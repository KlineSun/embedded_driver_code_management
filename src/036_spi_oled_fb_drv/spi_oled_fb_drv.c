#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/kmod.h>
#include <linux/fb.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/cdev.h>


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kline <kline_code@example,com>");
MODULE_DESCRIPTION("Test for virtual spi dac driver.");

#define DEBUG_LOG(fmt, ...) \
        printk(KERN_INFO "[%s line%d] %s(): "fmt"\n", \
            KBUILD_BASENAME, __LINE__, __FUNCTION__, ##__VA_ARGS__)

#define OLED_BPP (1)

#define OLED_CMD (0)
#define OLED_DATA (1)

#define OLED_INIT_OPT       (100)
#define OLED_SET_POS_OPT    (101)
#define OLED_CLEAR_OPT      (102)

#define OLED_POS(x, y) ((y << 8) | x) 

struct spi_oled_device {
    struct spi_device *spi_dev;
    struct cdev cdev;
    struct device *chrdev;
    u32 spi_freq;
    u32 spi_minor;
    u32 xres;
    u32 yres;
    u32 fps;
    struct gpio_desc *dc_gpiod;
    dev_t devno;
    struct class *cls;
    bool inited;

    u8 *spi_buf;
    u32 buf_size;
    struct fb_info *fb;
    spinlock_t fb_lock;
    struct task_struct *kthread;
};

static struct spi_oled_device *g_spi_oled = NULL;
static u32 g_pseudo_palette[16];


/**
 * @brief Write data into oled device
 * 
 * @param buf Data want to write
 * @param size Length of data. If flag is OLED_CMD, the size is fixed at 1. It means that cmd must be sent one by one.
 * @param flag Data type: 0 means OLED_CMD. Need dc_pin_ctrl(0) before write.
 *                        1 means OLED_DATA.Need dc_pin_ctrl(1) before write.
 * @return Return 0 on success, or negative number on failure.
*/
static int oled_wrire_common(u8 *buf, u32 size, u8 flag)
{
    struct spi_transfer	*xfers = NULL;
    struct spi_message	msg;
    int i = 0, err = 0;
    if (!buf || size <= 0) {
        DEBUG_LOG("Invalid parameter!");
        return -1;
    }

    if (!g_spi_oled) {
        DEBUG_LOG("Invalid spi oled device!");
        return -1;
    }

    if (flag == OLED_CMD) {
        gpiod_set_value(g_spi_oled->dc_gpiod, 1); // active状态，表示传输命令
        if (size > 1) {
            DEBUG_LOG("OLED device commands must be sent one by one!");
            return -1;
        }
        // DEBUG_LOG("cmd: 0x%x", *buf);
    } else if (flag == OLED_DATA) {
        gpiod_set_value(g_spi_oled->dc_gpiod, 0); // no active状态，表示传输数据
        // DEBUG_LOG("%u data transfer", size);
    } else {
        DEBUG_LOG("Invalid flag: %d", flag);
        return -1;
    }
    //DEBUG_LOG("Transfer %u spi messages!", size);

    // spi传输
    xfers = kcalloc(size, sizeof(struct spi_transfer), GFP_KERNEL);
    if (!xfers) {
        DEBUG_LOG("Alloc spi_transfer memory failed!");
        return -1;
    }

    spi_message_init(&msg);
    for (i = 0; i < size; i++) {
        xfers[i].len = 1; // 每一帧msg传输两字节数据
        xfers[i].tx_buf = &buf[i];
        xfers[i].speed_hz = g_spi_oled->spi_freq;
        spi_message_add_tail(&xfers[i], &msg);
    }

    // 发送msg
    err = spi_sync(g_spi_oled->spi_dev, &msg);
	if (err < 0) {
        DEBUG_LOG("spi_sync failed: %d", err);
        kfree(xfers);
		return err;
    }

    //DEBUG_LOG("Transfer end!");
    kfree(xfers);
    return size;
}

static int oled_write_cmd(u8 cmd)
{
    u8 val = cmd;
    return oled_wrire_common(&val, 1, OLED_CMD);
}

static int oled_write_data(u8 *data, u32 size)
{
    return oled_wrire_common(data, size, OLED_DATA);
}

static void oled_draw_position(u16 col, u16 page)
{
    if (col >= g_spi_oled->xres || page >= g_spi_oled->yres / 8) {
        DEBUG_LOG("Position out of the range!");
        return;
    }

    oled_write_cmd(0xb0+page); // set page
    oled_write_cmd(col & 0x0f); // set lower 4 bit of column position
    oled_write_cmd(((col&0xf0)>> 4) | 0x10); // set higher 4 bit of column position
}

static int oled_clear(u8 val)
{
    int i = 0, byte_count = 0;
    u8 *buf = NULL;

    if (!g_spi_oled) {
        DEBUG_LOG("Invalid spi oled device!");
        return -1;
    }

    byte_count = g_spi_oled->xres * g_spi_oled->yres / 8;
    buf = kzalloc(byte_count, GFP_KERNEL);
    if (!buf) {
        DEBUG_LOG("Alloc memory failed!");
        return -1;
    }
    memset(buf, val, byte_count);

    // set zero for each page
    for (i = 0; i < g_spi_oled->yres / 8; i++) {
        oled_draw_position(0, i);
        // 每次写一页空数据
        if (oled_write_data(buf + i*g_spi_oled->xres, g_spi_oled->xres) < 0) { 
            DEBUG_LOG("write clear data into oled device failed!");
            kfree(buf);
            return -1;
        }
    }
    kfree(buf);
    return 0;
}

static void oled_init(void)
{
    DEBUG_LOG("Enter!");

    oled_write_cmd(0xae);//关闭显示

    oled_write_cmd(0x00);//设置 lower column address
    oled_write_cmd(0x10);//设置 higher column address

    oled_write_cmd(0x40);//设置 display start line

    oled_write_cmd(0xB0);//设置page address

    oled_write_cmd(0x81);// contract control
    oled_write_cmd(0x66);//128

    oled_write_cmd(0xa1);//设置 segment remap

    oled_write_cmd(0xa6);//normal /reverse

    oled_write_cmd(0xa8);//multiple ratio
    oled_write_cmd(0x3f);//duty = 1/64

    oled_write_cmd(0xc8);//com scan direction

    oled_write_cmd(0xd3);//set displat offset
    oled_write_cmd(0x00);//

    oled_write_cmd(0xd5);//set osc division
    oled_write_cmd(0x80);//

    oled_write_cmd(0xd9);//ser pre-charge period
    oled_write_cmd(0x1f);//

    oled_write_cmd(0xda);//set com pins
    oled_write_cmd(0x12);//

    oled_write_cmd(0xdb);//set vcomh
    oled_write_cmd(0x30);//

    oled_write_cmd(0x8d);//set charge pump disable 
    oled_write_cmd(0x14);//

    oled_write_cmd(0x20);
    oled_write_cmd(0x2); // set Page Addressing Mode

    oled_clear(0xff); // clear display
    oled_write_cmd(0xaf);//set dispkay on
    g_spi_oled->inited = true;
    // 100ms Delay Recommended
    msleep(100);
}

void oled_close(void)
{
    DEBUG_LOG("Enter!");

    oled_clear(0); // clear display
    // Set Display off
    oled_write_cmd(0xAE);

    g_spi_oled->inited = false;
    // 100ms Delay Recommended
    msleep(100);
}

void oled_bitmap_print(struct spi_oled_device *oled)
{
    int x = 0, y = 0, pos = 0, bit = 0;
    int line_size = oled->xres + 1;
    u8 *line_buf = NULL;

    line_buf = kzalloc(line_size, GFP_KERNEL);
    if (!line_buf) {
        DEBUG_LOG("Alloc memory failed!");
        return;
    }

    // 逐行打印位图
    for (y = 0; y < oled->yres; y++) {
        for (x = 0; x < oled->xres; x++) {
            pos = (y / 8)*oled->xres + x; // page + col
            bit = y % 8;
            if (oled->spi_buf[pos] & (1 << bit))
                line_buf[x] = 0x2a; // *号
            else
                line_buf[x] = 0x2e; // 点号
        }
        line_buf[line_size] = '\0';
        printk("\t%s", line_buf);
    }
}

void print_fb(struct fb_info *fb)
{
    int x = 0, y = 0;
    unsigned int byte_idx = 0, bit_offset = 0, bpp = 0;
    char *line_buf = NULL;
    bool bit_offset_flg = false;
    bool pixel_active = false;
    unsigned char *pen = NULL;
    if (!fb) {
        DEBUG_LOG("Invalid parameter!");
        return;
    }

    line_buf = kzalloc(fb->var.xres + 1, GFP_KERNEL); // 逐行输出，因此申请一行的内存即可
    if (!line_buf) {
        DEBUG_LOG("Alloc bitmap print memory failed!");
        return;
    }

    bpp = fb->var.bits_per_pixel;
    bit_offset_flg = bpp % 8 ? true : false;
    printk("fb(%u*%u), flag=%d:", fb->var.xres, fb->var.yres, bit_offset_flg);
    for (y = 0; y < fb->var.yres; y++) {
        for (x = 0; x < fb->var.xres; x++) {
            byte_idx = y * fb->fix.line_length + x * bpp / 8;
            pen = fb->screen_base + byte_idx;
            bit_offset = 7 - x * bpp % 8;

            pixel_active = bit_offset_flg ? (*pen & (1 << bit_offset)) : (*pen != 0);
            line_buf[x] = pixel_active ? 0x2a : 0x2e;
        }
        line_buf[fb->var.xres] = '\0';
        printk("\t%s", line_buf);
        memset(line_buf, 0, fb->var.xres+1);
    }
    kfree(line_buf);
}

int fb_to_oled_layout(struct spi_oled_device *oled)
{
    int col = 0, page = 0, bit = 0;
    int fb_pos = 0, fb_bit = 0, oled_pos = 0, fb_pos_start;
    u8 fb_pen = 0;
    if (!oled) {
        DEBUG_LOG("Invalid parameter!");
        return -EINVAL;
    }

    print_fb(oled->fb);
    memset(oled->spi_buf, 0, oled->buf_size);
    // 逐页转化源buf
    for(page = 0; page < oled->yres/8; page++) {
        // 逐字节赋值
        for (col = 0; col < oled->xres; col++) {
            oled_pos = page * oled->xres + col; // oled_buf中对应字节的位置
            // 逐位赋值
            for (bit = 0; bit < 8; bit++) {
                fb_pos = (page * 8 + bit) * oled->fb->fix.line_length + col / 8; // 计算出在fb_buf中的位置
                fb_pen = oled->fb->screen_base[fb_pos]; // 取出对应的byte
                fb_bit = 7 - col % 8; // 计算位偏移量

                if (fb_pen & (1 << fb_bit))
                    oled->spi_buf[oled_pos] |= (1 << bit);
            }
            fb_pos_start = page*oled->fb->fix.line_length*8 + col / 8; // 计算出在fb_buf中的位置
            // printk("page%d, col%d, in buf[%d]=0x%x", page, col, oled_pos, oled->spi_buf[oled_pos]);
        }
    }

    // oled_bitmap_print(oled);
    return 0;
}

static int spi_oled_drv_open(struct inode *inode, struct file *file)
{
    int minor = iminor(inode);
    struct spi_oled_device *oled = container_of(inode->i_cdev, struct spi_oled_device, cdev);

    if (!oled) {
        DEBUG_LOG("Arguments transfer excepiton!");
        return -1;
    }

    DEBUG_LOG("Enter with minor: %d", minor);
    file->private_data = oled;
    return 0;
}


static long spi_oled_drv_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct inode *inode = file_inode(file);
    int minor = iminor(inode);
    u32 col = 0, page = 0;

    DEBUG_LOG("Enter ioctl with minor: %d", minor);

    switch (cmd)
    {
        case OLED_INIT_OPT: {
            /* code */
            oled_init();
            break;
        }
        case OLED_SET_POS_OPT: {
            /* code */
            col = arg & 0xff; // 低8位
            page = ((arg >> 8) & 0xff) / 8;
            oled_draw_position(col, page);
            break;
        }
        case OLED_CLEAR_OPT: {
            /* code */
            if (arg == 0xff) oled_clear(0xff);
            else oled_clear(0);
            break;
        }
        default:
            DEBUG_LOG("Unknown operation: %u", cmd);
            break;
    }

    DEBUG_LOG("Ioctrl success!");
    return 0;
}

static ssize_t spi_oled_drv_write(struct file *file, const char __user *buf, size_t size, loff_t *offset)
{
    struct inode *inode = file_inode(file);
    int minor = iminor(inode), ret = 0;
    struct spi_oled_device *oled = file->private_data;
    u8 *tx_buf = NULL;
    if (!oled) {
        DEBUG_LOG("Arguments transfer excepiton!");
        return -1;
    }
    if (size > oled->xres * oled->yres / 8) {
        DEBUG_LOG("write size out of range: (0~%u)", oled->xres * oled->yres / 8);
        return -EINVAL;
    }

    DEBUG_LOG("Enter with minor=%ds", minor);
    tx_buf = kzalloc(size, GFP_KERNEL);
    if (!tx_buf) {
        DEBUG_LOG("Alloc tx_buf memory failed!");
        return -ENOMEM;
    }
    copy_from_user(tx_buf, buf, size);

    ret = oled_write_data(tx_buf, size);
    if (ret < 0)
        DEBUG_LOG("Write data into oled failed!");

    kfree(tx_buf);
    return ret;
}

static int spi_oled_drv_close(struct inode *inode, struct file *file)
{
    int minor = iminor(inode);

    DEBUG_LOG("Enter with minor: %d", minor);
    return 0;
}

static struct file_operations spi_oled_drv_fop = {
    .owner =            THIS_MODULE,
    .open =             spi_oled_drv_open,
    .write =            spi_oled_drv_write,
    .unlocked_ioctl =   spi_oled_drv_ioctl,
    .release =          spi_oled_drv_close,
};

static inline unsigned int chan_to_field(unsigned int chan,
                    struct fb_bitfield *bf)
{
    chan &= 0xffff;
    chan >>= 16 - bf->length;
    return chan << bf->offset;
}

static int mylcd_setcolreg(unsigned regno,
                unsigned red, unsigned green, unsigned blue,
                unsigned transp, struct fb_info *info)
{
    unsigned int val;

    /* dprintk("setcol: regno=%d, rgb=%d,%d,%d\n",
        regno, red, green, blue); */

    switch (info->fix.visual) {
    case FB_VISUAL_TRUECOLOR:
        /* true-colour, use pseudo-palette */

        if (regno < 16) {
            u32 *pal = info->pseudo_palette;

            val  = chan_to_field(red,   &info->var.red);
            val |= chan_to_field(green, &info->var.green);
            val |= chan_to_field(blue,  &info->var.blue);

            pal[regno] = val;
        }
        break;

    default:
        return 1;	/* unknown type */
    }

    return 0;
}

static int oled_fb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
    // 确保参数合法（如分辨率、色深等）
    if (var->xres != g_spi_oled->xres || var->yres != g_spi_oled->yres) {
        DEBUG_LOG("Unsupported resolution: %ux%u (expected %ux%u)\n",
               var->xres, var->yres, g_spi_oled->xres, g_spi_oled->yres);
        return -EINVAL;
    }
    DEBUG_LOG("Enter with var: xres=%u, yres=%u, bpp=%u", var->xres, var->yres, var->bits_per_pixel);
    if (var->bits_per_pixel != OLED_BPP) {
        var->bits_per_pixel = OLED_BPP; // 强制设置为 1 BPP
    }
    return 0;
}

static int oled_fb_set_par(struct fb_info *info)
{
    DEBUG_LOG("Enter!");
    // 应用新的显示参数（可选）
    return 0;
}

static struct fb_ops oled_fb_ops = {
    .owner		    = THIS_MODULE,
    .fb_check_var   = oled_fb_check_var,  // 必须实现
    .fb_set_par     = oled_fb_set_par,    // 可选但推荐
    .fb_setcolreg	= mylcd_setcolreg, // 调试：添加fb的ops
    .fb_fillrect	= cfb_fillrect,
    .fb_copyarea	= cfb_copyarea,
    .fb_imageblit	= cfb_imageblit,
};

static int oled_refresh_thread_func(void *data)
{
    int i = 0;
    struct spi_oled_device *oled = data;
    u32 msec_delay = (u32)(1000 / oled->fps);
    // u8 ch = 0x55;
    if (!oled) {
        DEBUG_LOG("Invalid thread data!");
        return -EINVAL;
    }

    msec_delay *= 20; // 先做调试，避免打印太多
    DEBUG_LOG("Enter with delay %ums", msec_delay);
    while (!kthread_should_stop()) {
        if (!oled->inited) {
            msleep(20);
            continue;
        }

        // DEBUG_LOG("Refresh thread %i running!", current->pid);
        // DEBUG_LOG("ch = 0x%x", ch);
        // memset(oled->fb->screen_base, ch, oled->fb->fix.smem_len); // 调试转化功能
        // ch = ~ch;
        // 转化framebuffer数据为oled格式
        fb_to_oled_layout(oled);

        // 逐页填充到oled中去
        for (i = 0; i < oled->yres / 8; i++) {
            oled_draw_position(0, i);
            // 每次写一页空数据
            if (oled_write_data(oled->spi_buf + oled->xres*i, oled->xres) < 0) { 
                DEBUG_LOG("write clear data into oled device failed!");
                return -1;
            }
        }

        msleep(msec_delay);
    }
    return 0;
}

static int spi_oled_probe(struct spi_device *spi)
{
    struct device *dev = &spi->dev;
    struct device_node	*pn = spi->dev.of_node;
    int err = 0;
    dma_addr_t map_dma;

    if (!dev || !pn) {
        DEBUG_LOG("Invalid device node!");
        return -EINVAL;
    }

    DEBUG_LOG("Enter!");

    // 1、分配自定义结构体
    g_spi_oled = devm_kzalloc(dev, sizeof(*g_spi_oled), GFP_KERNEL);
    if (!g_spi_oled) {
        DEBUG_LOG("Alloc spi_oled_device memory failed!");
        return -ENOMEM;
    }

    // 2、获取设备树资源
    if (of_property_read_u32(pn, "reg", &g_spi_oled->spi_minor)) {
        DEBUG_LOG("Read spi_minor from dts failed!");
        return -EINVAL;
    }
    if (of_property_read_u32(pn, "spi-max-frequency", &g_spi_oled->spi_freq)) {
        DEBUG_LOG("Read frequency from dts failed!");
        return -EINVAL;
    }
    if (of_property_read_u32(pn, "xres", &g_spi_oled->xres)) {
        DEBUG_LOG("Read xres from dts failed!");
        return -EINVAL;
    }
    if (of_property_read_u32(pn, "yres", &g_spi_oled->yres)) {
        DEBUG_LOG("Read yres from dts failed!");
        return -EINVAL;
    }
    if (of_property_read_u32(pn, "fps", &g_spi_oled->fps)) {
        DEBUG_LOG("Get fps from dts failed!");
        return -EINVAL;
    }
    g_spi_oled->dc_gpiod = devm_gpiod_get(dev, "dc", GPIOD_OUT_HIGH);
    if (!g_spi_oled->dc_gpiod) {
        DEBUG_LOG("Get dc gpiod from dts failed!");
        return -EINVAL;
    }
    g_spi_oled->spi_dev = spi;
    g_spi_oled->inited = false;
    // 分配spi_buf
    g_spi_oled->buf_size = g_spi_oled->xres * g_spi_oled->yres / 8;
    g_spi_oled->spi_buf = devm_kzalloc(dev, g_spi_oled->buf_size, GFP_KERNEL);
    if (!g_spi_oled->spi_buf) {
        DEBUG_LOG("Alloc spi_buf memory failed!");
        return -ENOMEM;
    }
    DEBUG_LOG("spi_minor=%u, spi_frequency=%u, xres=%u, yres=%u, fps=%u, allocate %u bytes buffer",
                g_spi_oled->spi_minor, g_spi_oled->spi_freq,
                g_spi_oled->xres, g_spi_oled->yres, g_spi_oled->fps,
                g_spi_oled->buf_size);


    // 3、分配、设置、注册framebuffer
    // 3.1 分配fbinfo
    g_spi_oled->fb = framebuffer_alloc(sizeof(*g_spi_oled), dev);
    if (!g_spi_oled->fb) {
        DEBUG_LOG("Alloc framebuffer memory failed!");
        return -ENOMEM;
    }

    // 3.2 设置fbinfo
    /* 设置可变参数 */
    g_spi_oled->fb->var.xres = g_spi_oled->xres;
    g_spi_oled->fb->var.yres = g_spi_oled->yres;
    g_spi_oled->fb->var.xres_virtual = g_spi_oled->xres;
    g_spi_oled->fb->var.yres_virtual = g_spi_oled->yres;
    g_spi_oled->fb->var.bits_per_pixel = OLED_BPP;
    g_spi_oled->fb->var.grayscale = 1;       // 单色显示
    g_spi_oled->fb->var.red.offset = 0;      // 红色分量偏移（单色屏设为 0）
    g_spi_oled->fb->var.red.length = 1;      // 红色分量的位宽
    g_spi_oled->fb->var.green.offset = 0;    // 绿色分量偏移
    g_spi_oled->fb->var.green.length = 1;    // 绿色分量的位宽
    g_spi_oled->fb->var.blue.offset = 0;     // 蓝色分量偏移
    g_spi_oled->fb->var.blue.length = 1;     // 蓝色分量的位宽
    g_spi_oled->fb->var.transp.offset = 0;   // 透明度偏移
    g_spi_oled->fb->var.transp.length = 0;   // 透明度的位宽
    /* 设置显示模式（非必须，但建议） */
    g_spi_oled->fb->var.activate = FB_ACTIVATE_NOW;
    g_spi_oled->fb->var.height = -1;         // 物理高度（未知设为 -1）
    g_spi_oled->fb->var.width = -1;          // 物理宽度（未知设为 -1）

    /* 设置固定参数 */
    g_spi_oled->fb->fix.smem_len = (g_spi_oled->xres * g_spi_oled->yres * OLED_BPP) / 8;
    g_spi_oled->fb->fix.line_length = (g_spi_oled->xres * OLED_BPP) / 8;

    strncpy(g_spi_oled->fb->fix.id, pn->name, 16);
    g_spi_oled->fb->fix.id[15] = 0;

    /* 设置显存的虚拟地址和物理地址 */
    dma_set_coherent_mask(dev, DMA_BIT_MASK(32));
    DEBUG_LOG("dma_alloc_wc size: %u, line_length=%u", g_spi_oled->fb->fix.smem_len, g_spi_oled->fb->fix.line_length);
    g_spi_oled->fb->screen_base = dma_alloc_wc(dev, PAGE_ALIGN(g_spi_oled->fb->fix.smem_len), &map_dma, GFP_KERNEL);
    if (!g_spi_oled->fb->screen_base) {
        DEBUG_LOG("Alloc dma memory failed!");
        err = -ENOMEM;
        goto fb_release;
    }
    g_spi_oled->fb->fix.smem_start = map_dma;
    g_spi_oled->fb->fix.type = FB_TYPE_PACKED_PIXELS; // 调试：增加参数设置
	g_spi_oled->fb->fix.visual = FB_VISUAL_MONO10; // 调试：增加参数设置

    /* 设置fb_ops */
    g_spi_oled->fb->fbops = &oled_fb_ops;

    /* 其它设置 */
    g_spi_oled->fb->flags = FBINFO_FLAG_DEFAULT;
    // g_spi_oled->fb->pseudo_palette = kmalloc_array(16, sizeof(u32), GFP_KERNEL);
    g_spi_oled->fb->pseudo_palette = g_pseudo_palette;

    // 3.3 注册fbinfo
    err = register_framebuffer(g_spi_oled->fb);
    if (err) {
        DEBUG_LOG("Register framebuffer failed!");
        goto dma_free;
    }
    spin_lock_init(&g_spi_oled->fb_lock);

    // 4、初始化oled并清屏
    // 先初始化dc引脚，将dc状态设置为逻辑0(即no active)
    gpiod_set_value(g_spi_oled->dc_gpiod, 0);
    oled_init();

    // 5、创建设备节点
    err = alloc_chrdev_region(&g_spi_oled->devno, 0, 1, "spi_oled_drv");
    if (err != 0) {
        DEBUG_LOG("Alloc char device region failed!");
        goto fb_unregister;
    }

    cdev_init(&g_spi_oled->cdev, &spi_oled_drv_fop);
    if (cdev_add(&g_spi_oled->cdev, g_spi_oled->devno, 1)) {
        DEBUG_LOG("Add cdev failed!");
        err = -EINVAL;
        goto chrdev_free;
    }

    // class创建
    g_spi_oled->cls = class_create(THIS_MODULE, "spi_oled");
    if (IS_ERR(g_spi_oled->cls)) {
        DEBUG_LOG("Create class failed!");
        err = -EINVAL;
        goto cdev_free;
    }
    
    // 创建字符设备节点，传入g_btn_descs作为参数
    g_spi_oled->chrdev = device_create(g_spi_oled->cls, 
                                    dev,
                                    g_spi_oled->devno,
                                    g_spi_oled,
                                    "spi_oled");
    if (IS_ERR(g_spi_oled->chrdev)) {
        DEBUG_LOG("Create device node failed!");
        err = -EINVAL;
        goto class_free;
    }

    // 6、创建线程用于循环刷新oled
    g_spi_oled->kthread = kthread_run(oled_refresh_thread_func, g_spi_oled, "spi_oled_refresh");
    if (IS_ERR(g_spi_oled->kthread)) {
        DEBUG_LOG("Create refresh kthread failed!");
        err = -EINVAL;
        goto class_free;
    }

    DEBUG_LOG("Probe button success!");
    return 0;


class_free:
    class_destroy(g_spi_oled->cls);

cdev_free:
    cdev_del(&g_spi_oled->cdev);

chrdev_free:
    unregister_chrdev_region(g_spi_oled->devno, 1);

fb_unregister:
    if (g_spi_oled->fb)
        unregister_framebuffer(g_spi_oled->fb);

dma_free:
    if (g_spi_oled->fb->screen_base)
        dma_free_wc(dev, PAGE_ALIGN(g_spi_oled->fb->fix.smem_len),
                    g_spi_oled->fb->screen_base, g_spi_oled->fb->fix.smem_start);
fb_release:
    framebuffer_release(g_spi_oled->fb);
    return err;
}

static int spi_oled_remove(struct spi_device *spi)
{
    DEBUG_LOG("Enter!");

    if (g_spi_oled->kthread)
        kthread_stop(g_spi_oled->kthread);

    device_destroy(g_spi_oled->cls, g_spi_oled->devno);
    class_destroy(g_spi_oled->cls);
    cdev_del(&g_spi_oled->cdev);
    unregister_chrdev_region(g_spi_oled->devno, 1);

    oled_close();
    if (g_spi_oled->fb)
        unregister_framebuffer(g_spi_oled->fb);

    if (g_spi_oled->fb->screen_base)
        dma_free_wc(&g_spi_oled->spi_dev->dev, PAGE_ALIGN(g_spi_oled->fb->fix.smem_len),
                    g_spi_oled->fb->screen_base, g_spi_oled->fb->fix.smem_start);

    if (g_spi_oled->fb)
        framebuffer_release(g_spi_oled->fb);

    return 0;
}

static const struct of_device_id ask100_spi_oled_of_match[] = {
	{ .compatible = "100ask,ssd1306_oled", },
	{},
};

static struct spi_driver spi_oled_driver = {
	.probe		= spi_oled_probe,
	.remove		= spi_oled_remove,
	.driver		= {
		.name	= "100ask_spi_oled_drv",
        .of_match_table = ask100_spi_oled_of_match,
	},
};

static int __init spi_oled_init(void)
{
    DEBUG_LOG("Enter!");
	return spi_register_driver(&spi_oled_driver);
}
module_init(spi_oled_init);

static void __exit spi_oled_exit(void)
{
    DEBUG_LOG("Enter!");
	spi_unregister_driver(&spi_oled_driver);
}
module_exit(spi_oled_exit);
