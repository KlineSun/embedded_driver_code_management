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
#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <asm/mach/map.h>
#include <linux/fb.h>
#include <linux/dma-mapping.h>
#include <linux/clk.h>
#include <linux/bits.h>
#include <video/of_display_timing.h>
#include <video/display_timing.h> 
#include "lcd_fb_mutifb_drv.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kline <kline_code@example,com>");
MODULE_DESCRIPTION("Test for button bus driver");

/*************************多重fb优化流程*****************************/
/**
 * 1. 注册n个fb容量的内存，用于显存
 *  使用接口：dma_alloc_writecombine
 *  单个fb大小：xres * yres * bpp / 8
 *  对应fb参数：fb->fix.smem_len = fb->fix.line_length * fb->var.yres_virtual;
 * 2. 在fb_info中设置
 *  fb->fb.var.xres		= xres;
    fb->fb.var.yres		= yres;
    fb->fb.var.xres_virtual	= xres;
    fb->fb.var.yres_virtual	= yres * n;
    fb->fb.fix.smem_len = n * (xres * yres * bpp / 8);
 * 
 * 3. 在APP中，通过获得var和fix参数，计算获得驱动提供的fb数量n：
 *  n = fb->fb.fix.smem_len / (fb->fb.var.xres * fb->fb.var.yres * fb->fb.var.bits_per_pixel / 8);
 * 
 * 4. APP中填充好一帧fb数据
 * 
 * 5. 通知驱动切换FB，使用:
 *  ioctrl(fd, FBIOPAN_DISPLAY, data);
 * 
 * 6. 驱动中，会经过ioctrl调用到fb_ops中的fb_pan_display函数，在该函数中，完成fb的切换，重新设置LTDC寄存器中设置的fb基地址
 * 
 */



static struct fb_info *g_fbinfo = NULL;
static unsigned int pseudo_palette[16] = {0};

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

static struct fb_ops stm32_fb_ops = {
    .owner		    = THIS_MODULE,
    // 非常规file接口
	.fb_setcolreg	= mylcd_setcolreg,
    .fb_fillrect	= cfb_fillrect,
    .fb_copyarea	= cfb_copyarea,
    .fb_imageblit	= cfb_imageblit,
};

void stm32_ltdc_switch_ctrl(struct fb_info *fbinfo, bool status)
{
    struct stm32_ltdc_info *ltdc_info = fbinfo->par;
    stm32_ltdc_regs *ltdc_regs;
    if (!ltdc_info) {
        DEBUG_LOG("Invalid stm32_ltdc_info!");
        return;
    }

    ltdc_regs = ltdc_info->regs_vaddr;
    if (!ltdc_regs) {
        DEBUG_LOG("Invalid ltdc registers address!");
        return;
    }

    if (status) {
        // enable
        //问题三：未设置LTDC_SRCR寄存器
        ltdc_regs->LTDC_SRCR |= 1;
        ltdc_set_bit(&ltdc_regs->LTDC_GCR, 0);
    } else {
        // disable
        ltdc_clear_bit(&ltdc_regs->LTDC_GCR, 0);
    }
    DEBUG_LOG("Cntrol LTDC %s", status ? "enable" : "disable");
}

void stm32_ltdc_regs_init(struct fb_info *fbinfo)
{
    struct stm32_ltdc_info *ltdc_info = fbinfo->par;
    // int hsync_active = 0, vsync_active = 0, pclk_active = 0, de_active = 0;
    unsigned int bpp = 0;
    stm32_ltdc_regs *ltdc_regs;
    struct display_timing *dt = NULL;

    if (!ltdc_info) {
        DEBUG_LOG("Invalid stm32_ltdc_info!");
        return;
    }

    DEBUG_LOG("Enter!");
    ltdc_regs = ltdc_info->regs_vaddr;
    dt = ltdc_info->dtms->timings[ltdc_info->dtms->native_mode];
    bpp = fbinfo->var.bits_per_pixel;
    if (!ltdc_regs || !dt) {
        DEBUG_LOG("Invalid ltdc info!");
        return;
    }

    // 设置hsync_len和vsync_len
    // 要求格式：HSYNC width - 1，VSYNC height - 1
    DEBUG_LOG("Setting hsync_len!");
    ltdc_regs->LTDC_SSCR = ((dt->hsync_len.typ) << 16) | (dt->vsync_len.typ);


    // 要求格式：HSYNC width + HBP - 1，VSYNC height + VBP - 1
    ltdc_regs->LTDC_BPCR = 0;
    ltdc_regs->LTDC_BPCR = ((dt->hsync_len.typ + dt->hback_porch.typ - 1) << 16)
                            | (dt->vsync_len.typ + dt->vback_porch.typ - 1);

    // 设置hactive和vactive
    // 要求格式：(HSYNC width + HBP + active width - 1)，(VSYNC height + VBP + active height - 1)
    ltdc_regs->LTDC_AWCR = 0;
    ltdc_regs->LTDC_AWCR = ((dt->hsync_len.typ + dt->hback_porch.typ + dt->hactive.typ - 1) << 16)
                            | (dt->vsync_len.typ + dt->vback_porch.typ + dt->vactive.typ - 1);

    // 设置total width
    // 要求格式：(HSYNC width + HBP + active width + HFP - 1)，(VSYNC height + VBP + active height + VFP - 1)
    ltdc_regs->LTDC_TWCR = 0;
    ltdc_regs->LTDC_TWCR = ((dt->hsync_len.typ + dt->hback_porch.typ + dt->hactive.typ + dt->hfront_porch.typ - 1) << 16)
                            | (dt->vsync_len.typ + dt->vback_porch.typ + dt->vactive.typ + dt->vfront_porch.typ - 1);

    // 调整背光背景色
    // 要求格式：BCRED[7:0] BCGREEN[7:0] BCBLUE[7:0]
    // DEBUG_LOG("Setting background!");
    // ltdc_regs->LTDC_BCCR = ARGB8888_COMBINE(0, 0, 0, 0xff);; // 0x00ffff

    // 设置极性
    DEBUG_LOG("Setting polarity!");
    ltdc_regs->LTDC_GCR &= ~(0xf << 28);
    if (dt->flags & DISPLAY_FLAGS_HSYNC_HIGH) ltdc_set_bit(&ltdc_regs->LTDC_GCR, 31); // set 1
    else ltdc_clear_bit(&ltdc_regs->LTDC_GCR, 31); // clear 0

    if (dt->flags & DISPLAY_FLAGS_VSYNC_HIGH) ltdc_set_bit(&ltdc_regs->LTDC_GCR, 30); // set 1
    else ltdc_clear_bit(&ltdc_regs->LTDC_GCR, 30); // clear 0

    if (dt->flags & DISPLAY_FLAGS_DE_HIGH) ltdc_set_bit(&ltdc_regs->LTDC_GCR, 29); // set 1
    else ltdc_clear_bit(&ltdc_regs->LTDC_GCR, 29); // clear 0

    if (dt->flags & DISPLAY_FLAGS_PIXDATA_POSEDGE) ltdc_set_bit(&ltdc_regs->LTDC_GCR, 28); // set 1
    else ltdc_clear_bit(&ltdc_regs->LTDC_GCR, 28); // clear 0

    // 问题一：设置图层宽高寄存器高16位和低16位顺序颠倒
    // 设置图层1宽度
    ltdc_regs->LTDC_L1WHPCR = ((dt->hsync_len.typ + dt->hback_porch.typ + dt->hactive.typ - 1) << 16) | (dt->hsync_len.typ + dt->hback_porch.typ);
    // 设置图层1高度
    ltdc_regs->LTDC_L1WVPCR = ((dt->vsync_len.typ + dt->vback_porch.typ + dt->vactive.typ - 1) << 16) | (dt->vsync_len.typ + dt->vback_porch.typ);

    // 问题二：LTDC_L1CFBLR和LTDC_L1CFBLNR寄存器未设置
    ltdc_regs->LTDC_L1CFBLR = (dt->hactive.typ * (bpp>>3) + 7) | (dt->hactive.typ * (bpp>>3))<< 16;
	ltdc_regs->LTDC_L1CFBLNR = dt->vactive.typ;/*显存总共的行数*/

    // 设置图层1对比度
    ltdc_regs->LTDC_L1CACR |= 0xff;

    // 设置图层1 pixel format
    DEBUG_LOG("Setting pixel format!");
    ltdc_regs->LTDC_L1PFCR = 0;
    if (bpp == 24 || bpp == 32) {
        ltdc_regs->LTDC_L1PFCR |= 0b000;
    } else if (bpp == 16) {
        ltdc_regs->LTDC_L1PFCR |= 0b010;
    } else {
        DEBUG_LOG("Unsupport bpp, use default value:  0b000");
        ltdc_regs->LTDC_L1PFCR |= 0b000;
    }

    // 设置图层1默认颜色
    // ltdc_regs->LTDC_L1DCCR = ARGB8888_COMBINE(0xff, 0, 0xff, 0);

    // 设置图层1混合因子
    ltdc_regs->LTDC_L1BFCR = (0b100 << 8) | (0b101 << 0);

    // 设置图层1 framebuffer 基地址
    ltdc_regs->LTDC_L1CFBAR = fbinfo->fix.smem_start;

    // 使能图层1
    ltdc_set_bit(&ltdc_regs->LTDC_L1CR, 0);
    DEBUG_LOG("Ending registers config!");
}

static int stm32mp157_map_video_memory(struct fb_info *info)
{
    dma_addr_t map_dma;
    unsigned map_size = info->fix.smem_len;
    DEBUG_LOG("Enter!");

    info->screen_base = dma_alloc_wc(info->device, PAGE_ALIGN(map_size), &map_dma, GFP_KERNEL);
    if (info->screen_base) {
        /* prevent initial garbage on screen */
        memset(info->screen_base, 0xff, map_size);
        info->fix.smem_start = map_dma;

        DEBUG_LOG("map_video_memory: dma=%08lx cpu=%p size=%08x\n",
            info->fix.smem_start, info->screen_base, map_size);
    }
    return info->screen_base ? 0 : -ENOMEM;
}

static inline void stm32mp157_unmap_video_memory(struct fb_info *info)
{
    if (!info) {
        DEBUG_LOG("Invalid fb_info!");
        return;
    }
    DEBUG_LOG("Enter!");
    dma_free_wc(info->device, PAGE_ALIGN(info->fix.smem_len),
            info->screen_base, info->fix.smem_start);
}

int stm32mp157_lcd_probe(struct platform_device *pdev)
{
    struct device		*dev = NULL;
    struct device_node	*pn = NULL, *disp_pn = NULL;
    u32 xres, yres, bpp, bus_width;
    int err = 0;
    struct stm32_ltdc_info *stm32_ltdc = NULL;
    struct display_timings *disp_timings = NULL;
    struct display_timing *dt = NULL;
    struct gpio_desc  *bl_gpiod = NULL;
    struct clk *pixel_clk = NULL;
    struct resource *res;
    void __iomem *regs;

    DEBUG_LOG("Enter!");
    if (!pdev) {
        DEBUG_LOG("Invalid platform device!");
        return -1;
    }

    dev = &(pdev->dev);
    pn = dev->of_node;
    if (!pn) {
        DEBUG_LOG("Cannot get device node from platform device!");
        return -1;
    }
    DEBUG_LOG("get device node: name=%s, path=%s, id=%d", pn->name, pn->full_name, pn->phandle);

    /**************1.获取LCD参数 begin********************/
    // 获取节点中的reg寄存器地址
    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    if (!res) {
        DEBUG_LOG("Failed to get MEM resource!");
        return -ENODEV;
    }
    regs = devm_ioremap(dev, res->start, resource_size(res));
    // regs = devm_ioremap_resource(dev, res);
    if (!regs) {
        DEBUG_LOG("Failed to ioremap registers!");
        return -ENOMEM;
    }

    // 获取display对应子节点
    disp_pn = of_get_child_by_name(pn, "display");
    if (!disp_pn) {
        DEBUG_LOG("Cannot get display device node from parent node!");
        return -1;
    }

    // 从子节点中获取公共属性
    err = of_property_read_u32(disp_pn, "bits-per-pixel",&bpp );
    if (err < 0) {
        DEBUG_LOG("Cannot get property from display device node: %d", err);
        goto err_display_node;
    }
    err = of_property_read_u32(disp_pn, "bus-width",&bus_width );
    if (err < 0) {
        DEBUG_LOG("Cannot get property from display device node: %d", err);
        goto err_display_node;
    }

    // 获取timmings
    disp_timings = of_get_display_timings(disp_pn);
    if (!disp_timings) {
        err = PTR_ERR(disp_timings);
        DEBUG_LOG("Cannot get diosplay timming from display device node!");
        goto err_display_node;
    }

    // 取timings数组中的第native_mode个
    dt = disp_timings->timings[disp_timings->native_mode];
    if (!dt) {
        err = PTR_ERR(dt);
        DEBUG_LOG("No valid timing in timings!");
        goto err_timings;
    }
    /**************1.获取LCD参数 end********************/

    /**************2.设置ltdc参数 begin********************/
    // 获取gpiod
    bl_gpiod = devm_gpiod_get(dev, "backlight", GPIOD_OUT_HIGH);
    if (IS_ERR(bl_gpiod)) {
        err = PTR_ERR(bl_gpiod);
        DEBUG_LOG("Failed to get backlight gpio: %d", err);
        goto err_timings;
    }
    // 设置输出模式
    err = gpiod_direction_output(bl_gpiod, 1);
    if (err) {
        DEBUG_LOG("Failed to set gpio direction as output!");
        goto err_timings;
    }

    // 获取clk
    pixel_clk = devm_clk_get(dev, "lcd");
    if (IS_ERR(pixel_clk)) {
        err = PTR_ERR(pixel_clk);
        DEBUG_LOG("Failed to get LCD clock!");
        goto err_timings;
    }

    // 设置rate
    err = clk_set_rate(pixel_clk,  dt->pixelclock.typ);
    if (err < 0) {
        DEBUG_LOG("Cannot set frequency (%dHz) for pixel clk", dt->pixelclock.typ);
        goto err_timings;
    }

    // 使能clk
    err = clk_prepare_enable(pixel_clk);
    if (err) {
        DEBUG_LOG("Failed to enable LCD clock");
        goto err_timings;
    }
    /**************2.设置ltdc参数 end********************/

    /**************3.设置fbinfo参数 begin********************/
    /* 分配fbinfo */
    g_fbinfo = framebuffer_alloc(sizeof(struct stm32_ltdc_info), dev);
    if (!g_fbinfo) {
        DEBUG_LOG("Alloc framebuffer memory failed!");
        err = -ENOMEM;
        goto err_timings;
    }

    /* 设置stm32私有的ltdc数据 */
    stm32_ltdc = g_fbinfo->par;
    stm32_ltdc->dev = dev;
    stm32_ltdc->bl_gpio = bl_gpiod;
    stm32_ltdc->px_clk = pixel_clk;
    stm32_ltdc->dtms = disp_timings;
    stm32_ltdc->regs_vaddr = (stm32_ltdc_regs *)regs;

    /* 从timing中获取分辨率 */
    xres = dt->hactive.typ;
    yres = dt->vactive.typ;

    DEBUG_LOG("get info: xres=%d, yres=%d, bpp=%d", xres, yres, bpp);
    /* 设置可变参数 */
    g_fbinfo->var.xres = xres;
    g_fbinfo->var.yres = yres;
    g_fbinfo->var.xres_virtual = xres;
    g_fbinfo->var.yres_virtual = yres;
    g_fbinfo->var.bits_per_pixel = bpp;
    if (bpp == 24 || bpp == 32) {
        // ARGB888
        g_fbinfo->var.red.length = 8;
        g_fbinfo->var.red.offset = 16;

        g_fbinfo->var.green.length = 8;
        g_fbinfo->var.green.offset = 8;

        g_fbinfo->var.blue.length = 8;
        g_fbinfo->var.blue.offset = 0;
    } else if (bpp == 16) {
        // RGB565
        g_fbinfo->var.red.length = 5;
        g_fbinfo->var.red.offset = 11;

        g_fbinfo->var.green.length = 6;
        g_fbinfo->var.green.offset = 5;

        g_fbinfo->var.blue.length = 5;
        g_fbinfo->var.blue.offset = 0;
    } else {
        DEBUG_LOG("unsupport bpp: %d", bpp);
        err = -EPERM;
        goto fbinfo_free;
    }

    /* 设置固定参数 */
    if (bpp == 24) {
        g_fbinfo->fix.smem_len = xres * yres * 4;
        g_fbinfo->fix.line_length = xres * 4;
    } else {
        g_fbinfo->fix.smem_len = (xres * yres * bpp) / 8;
        g_fbinfo->fix.line_length = (xres * bpp) / 8;
    }
    strcpy(g_fbinfo->fix.id, "100ask_lcd");
    g_fbinfo->fix.id[MAX_LCD_NAME_LEN - 1] = 0;
    g_fbinfo->fix.type	    = FB_TYPE_PACKED_PIXELS;
    g_fbinfo->fix.visual = FB_VISUAL_TRUECOLOR;
    g_fbinfo->fix.type_aux	    = 0;
    g_fbinfo->fix.xpanstep	    = 0;
    g_fbinfo->fix.ypanstep	    = 0;
    g_fbinfo->fix.ywrapstep	    = 0;
    g_fbinfo->fix.accel	    = FB_ACCEL_NONE;

    /* 设置显存的虚拟地址和物理地址 */
    if (stm32mp157_map_video_memory(g_fbinfo)) {
        DEBUG_LOG("Alloc vedio memory failed!");
        err = -ENOMEM;
        goto fbinfo_free;
    }

    /* 设置fb_ops */
    g_fbinfo->fbops = &stm32_fb_ops;

    /* 其它设置 */
    g_fbinfo->flags = FBINFO_FLAG_DEFAULT;
    g_fbinfo->pseudo_palette = pseudo_palette;
    /**************3.设置fbinfo参数 end********************/

    /**************4.注册fbinfo begin********************/
    err = register_framebuffer(g_fbinfo);
    if (err < 0) {
        DEBUG_LOG("Failed to register framebuffer device: %d", err);
        goto vmem_free;
    }
    /**************4.注册fbinfo end********************/

    /**************5. LTDC硬件编程 begin********************/
    DEBUG_LOG("Start hardware program!");
    // 初始化LTDC寄存器
    stm32_ltdc_regs_init(g_fbinfo);

    // 使能LTDC
    stm32_ltdc_switch_ctrl(g_fbinfo, true);

    // 使能backlight gpio输出高电平
    gpiod_set_value(stm32_ltdc->bl_gpio, 1);
    /**************5. LTDC硬件编程 end********************/
    DEBUG_LOG("Probe success!");
    return 0;

vmem_free:
    if (g_fbinfo)
        stm32mp157_unmap_video_memory(g_fbinfo);
fbinfo_free:
    if (g_fbinfo)
        framebuffer_release(g_fbinfo);
err_timings:
    if (disp_timings)
        display_timings_release(disp_timings);
err_display_node:
    if (disp_pn)
        of_node_put(disp_pn);
    return err;
}

int stm32mp157_lcd_remove(struct platform_device *pdev)
{
    struct stm32_ltdc_info *stm32_ltdc = (struct stm32_ltdc_info *)g_fbinfo->par;
    DEBUG_LOG("Enter!");

    //关闭背光
    gpiod_set_value(stm32_ltdc->bl_gpio, 0);
    // 关闭LTDC
    stm32_ltdc_switch_ctrl(g_fbinfo, false);

    // 向fbmem.c反注册fbinfo
    if (g_fbinfo)
        unregister_framebuffer(g_fbinfo);

    // 释放显存
    DEBUG_LOG("Free framebuffer!");
    if (g_fbinfo)
        stm32mp157_unmap_video_memory(g_fbinfo);

    // 释放fbinfo
    DEBUG_LOG("Free fb_info!");
    if (g_fbinfo)
        framebuffer_release(g_fbinfo);

    //释放timings
    if (stm32_ltdc->dtms)
        display_timings_release(stm32_ltdc->dtms);
    DEBUG_LOG("end!");
    return 0;
}

static const struct of_device_id ask100_lcd_of_match[] = {
    { .compatible = "100ask,lcd_dtb_drv", },
    {},
};

static struct platform_driver stm32mp157_lcd_driver = {
    .probe		= stm32mp157_lcd_probe,
    .remove		= stm32mp157_lcd_remove,
    .driver		= {
        .name	= "100ask_fb",
        .of_match_table = ask100_lcd_of_match,
    },
};

module_platform_driver(stm32mp157_lcd_driver);
