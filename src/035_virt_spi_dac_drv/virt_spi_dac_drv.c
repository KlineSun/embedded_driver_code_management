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
#include <asm/irq.h>
#include <linux/irq.h>
#include <asm/io.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/interrupt.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/serial_core.h>
#include <linux/spi/spi.h>
#include <linux/cdev.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kline <kline_code@example,com>");
MODULE_DESCRIPTION("Test for virtual spi dac driver.");

#define DEBUG_LOG(fmt, ...) \
        printk(KERN_INFO "[%s line%d] %s(): "fmt"\n", \
            KBUILD_BASENAME, __LINE__, __FUNCTION__, ##__VA_ARGS__)

#define SPI_DAC_RX (0)
#define SPI_DAC_TX (1)
#define SPI_DAC_RDWR (2)
#define SPI_DAC_MSG_DATA_LEN    (2)
#define SPI_DAC_MSG_FLAG_OFFSET (0)
#define SPI_DAC_MSG_CNT_OFFSET  (8)
#define SPI_DAC_MSG(flag, cnt) (((cnt & 0xff) << SPI_DAC_MSG_CNT_OFFSET) | ((flag & 0xff) << SPI_DAC_MSG_FLAG_OFFSET))

#define SPI_DAC_MSG_CNT(cmd)  ((cmd >> SPI_DAC_MSG_CNT_OFFSET) & 0xff)
#define SPI_DAC_MSG_FLAG(cmd) ((cmd >> SPI_DAC_MSG_FLAG_OFFSET) & 0xff)


struct spi_dac_device {
    struct device *parent_dev;
    struct device *chrdev;
    struct cdev cdev;
    u32 spi_freq;
    struct spi_device *spi_dev;
    u32 spi_minor;
    dev_t devno;
    struct class *cls;
};

static struct spi_dac_device *g_spi_dac = NULL;

static int spi_dac_drv_open(struct inode *inode, struct file *file)
{
    int minor = iminor(inode);
    struct spi_dac_device *spidac = container_of(inode->i_cdev, struct spi_dac_device, cdev);

    if (!spidac) {
        DEBUG_LOG("Arguments transfer excepiton!");
        return -1;
    }

    DEBUG_LOG("Enter with minor: %d", minor);
    file->private_data = spidac;
    return 0;
}

uint16_t convert_dac_data(uint16_t val, int mode)
{
    uint16_t result = 0;
    uint8_t  high = 0, low = 0;

    if (mode == SPI_DAC_TX) {
        result = (val << 2) & 0xffc; // 末尾两位固定为0，左移两位
        high = (result >> 8) & 0xff;
        low = result & 0xff;
        result = ((low & 0xff) << 8) | (high & 0xff); // dac先从MSB开始传输，因此调整位置
    } else if (mode == SPI_DAC_RX) {
        high = (val >> 8) & 0xff;
        low = val & 0xff;
        result = ((low & 0xff) << 8) | (high & 0xff); // dac先从MSB开始传输，因此调整位置
        result >>= 2; // 每个数据末尾两位都是0，无含义的数据，丢弃
    } else {
        DEBUG_LOG("Unsupport mode: %d", mode);
        return val;
    }
    DEBUG_LOG("convert %hu -> %hu", val, result);
    return result;
}

static long spi_dac_drv_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct inode *inode = file_inode(file);
    int minor = iminor(inode);
    struct spi_dac_device *spi_dac = file->private_data;
    struct device *dev = spi_dac->parent_dev;
    int flag = 0, msg_count = 0, i = 0, err = 0;
    struct spi_transfer	*xfers = NULL;
    struct spi_message	msg;
    u16 *rx_buf = NULL, *tx_buf = NULL;


    if (!spi_dac || !dev) {
        DEBUG_LOG("Arguments transfer excepiton!");
        return -EINVAL;
    }

    // 取出cmd中包含的flag和数量信息
    flag = SPI_DAC_MSG_FLAG(cmd);
    msg_count = SPI_DAC_MSG_CNT(cmd);
    DEBUG_LOG("Enter with minor: %d, flag=%d, msg_count=%d", minor, flag, msg_count);

    // 分配对应数量的spi_transfer
    xfers = devm_kcalloc(dev, msg_count, sizeof(struct spi_transfer), GFP_KERNEL);
    rx_buf = devm_kcalloc(dev, msg_count, sizeof(u16), GFP_KERNEL);
    tx_buf = devm_kcalloc(dev, msg_count, sizeof(u16), GFP_KERNEL);
    if (!xfers || !rx_buf || !tx_buf) {
        DEBUG_LOG("Alloc msg memory failed!");
        return -ENOMEM;
    }

    // 从用户空间接收数据
    if ((flag == SPI_DAC_TX || flag == SPI_DAC_RDWR)
        && copy_from_user(tx_buf, (const u8 __user *)arg, msg_count*SPI_DAC_MSG_DATA_LEN)) {
        DEBUG_LOG("copy data from user space failed!");
        err = EIO;
        goto err_handle;
    }


    // 初始化msg
    spi_message_init(&msg);
    // 逐个填充spi_transfer结构体
    for (i = 0; i < msg_count; i++) {
        xfers[i].len = SPI_DAC_MSG_DATA_LEN; // 每一帧msg传输两字节数据
        switch (flag)
        {
            case SPI_DAC_RX: {
                xfers[i].rx_buf = &rx_buf[i];
                break;
            }
            case SPI_DAC_TX: {
                // 处理用户空间传输过来的数据，数据格式需要符合dac要求的数据格式
                DEBUG_LOG("Transfer value: %hu", tx_buf[i]);
                tx_buf[i] = convert_dac_data(tx_buf[i], SPI_DAC_TX);
                xfers[i].tx_buf = &tx_buf[i];
                break;
            }
            case SPI_DAC_RDWR: {
                DEBUG_LOG("Transfer value: %hu", tx_buf[i]);
                tx_buf[i] = convert_dac_data(tx_buf[i], SPI_DAC_TX);
                xfers[i].rx_buf = &rx_buf[i];
                xfers[i].tx_buf = &tx_buf[i];
                break;
            }
            default:
                break;
        }

        // spi_transfe公共成员填充
        xfers[i].speed_hz = spi_dac->spi_freq;
        spi_message_add_tail(&xfers[i], &msg);
    }

    // 发送msg
    err = spi_sync(spi_dac->spi_dev, &msg);
	if (err < 0) {
        DEBUG_LOG("spi_sync failed: %d", err);
		goto err_handle;
    }

    // 处理从spi设备中返回的数据
    for (i = 0; i < msg_count; i++) {
        rx_buf[i] = convert_dac_data(rx_buf[i], SPI_DAC_RX);
    }

    // 返回数据给用户空间
    if (flag == SPI_DAC_RX || flag == SPI_DAC_RDWR) {
        if (copy_to_user((u8 __user *)(uintptr_t)arg,
                        rx_buf,
                        msg_count*SPI_DAC_MSG_DATA_LEN)) {
            err = -EFAULT;
            goto err_handle;
        }
    }

    DEBUG_LOG("Ioctrl success!");
    return msg_count;

err_handle:
    DEBUG_LOG("Ioctrl error!");
    return err;
}

static int spi_dac_drv_close(struct inode *inode, struct file *file)
{
    int minor = iminor(inode);

    DEBUG_LOG("Enter with minor: %d", minor);
    return 0;
}

static struct file_operations spi_dac_drv_fop = {
    .owner =            THIS_MODULE,
    .open =             spi_dac_drv_open,
    .unlocked_ioctl =   spi_dac_drv_ioctl,
    .release =          spi_dac_drv_close,
};

static int spi_dac_probe(struct spi_device *spi)
{
    struct device *dev = &spi->dev;
    struct device_node	*pn = spi->dev.of_node;
    int err = 0;

    if (!dev || !pn) {
        DEBUG_LOG("Invalid device node!");
        return -EINVAL;
    }

    DEBUG_LOG("Enter!");

    g_spi_dac = devm_kzalloc(dev, sizeof(*g_spi_dac), GFP_KERNEL);
    if (!g_spi_dac) {
        DEBUG_LOG("Alloc spi_dac_device memory failed!");
        return -ENOMEM;
    }
    g_spi_dac->spi_dev = spi;
    g_spi_dac->parent_dev = dev;

    // 获取设备树资源
    if (of_property_read_u32(pn, "reg", &g_spi_dac->spi_minor)) {
        DEBUG_LOG("Read spi_minor from dts failed!");
        return -ENOMEM;
    }

    if (of_property_read_u32(pn, "spi-max-frequency", &g_spi_dac->spi_freq)) {
        DEBUG_LOG("Read spi_minor from dts failed!");
        return -ENOMEM;
    }
    DEBUG_LOG("spi_minor=%u, spi_frequency=%u", g_spi_dac->spi_minor, g_spi_dac->spi_freq);

    err = alloc_chrdev_region(&g_spi_dac->devno, 0, 1, "spi_dac_drv");
    if (err != 0) {
        DEBUG_LOG("Alloc char device region failed!");
        return err;
    }

    cdev_init(&g_spi_dac->cdev, &spi_dac_drv_fop);
    if (cdev_add(&g_spi_dac->cdev, g_spi_dac->devno, 1)) {
        DEBUG_LOG("Add cdev failed!");
        err = -EINVAL;
        goto chrdev_free;
    }

    // class创建
    g_spi_dac->cls = class_create(THIS_MODULE, "spi_dac");
    if (IS_ERR(g_spi_dac->cls)) {
        DEBUG_LOG("Create class failed!");
        err = -EINVAL;
        goto cdev_free;
    }

    
    // 创建字符设备节点，传入g_btn_descs作为参数
    g_spi_dac->chrdev = device_create(g_spi_dac->cls, 
                                    dev,
                                    g_spi_dac->devno,
                                    g_spi_dac,
                                    "spi_dac");
    if (IS_ERR(g_spi_dac->chrdev)) {
        err = -EINVAL;
        goto class_free;
    }
    DEBUG_LOG("Probe button success!");
    return 0;

class_free:
    class_destroy(g_spi_dac->cls);

cdev_free:
    cdev_del(&g_spi_dac->cdev);

chrdev_free:
    unregister_chrdev_region(g_spi_dac->devno, 1);
    return err;
}

static int spi_dac_remove(struct spi_device *spi)
{
    DEBUG_LOG("Enter!");

    device_destroy(g_spi_dac->cls, g_spi_dac->devno);

    class_destroy(g_spi_dac->cls);

    cdev_del(&g_spi_dac->cdev);
    
    unregister_chrdev_region(g_spi_dac->devno, 1);

    return 0;
}

static const struct of_device_id ask100_spi_dac_of_match[] = {
	{ .compatible = "100ask,tlc_dac", },
	{},
};

static struct spi_driver spi_dac_driver = {
	.probe		= spi_dac_probe,
	.remove		= spi_dac_remove,
	.driver		= {
		.name	= "100ask_spi_dac_drv",
        .of_match_table = ask100_spi_dac_of_match,
	},
};

static int __init spidac_init(void)
{
    DEBUG_LOG("Enter!");
	return spi_register_driver(&spi_dac_driver);
}
module_init(spidac_init);

static void __exit spidac_exit(void)
{
    DEBUG_LOG("Enter!");
	spi_unregister_driver(&spi_dac_driver);
}
module_exit(spidac_exit);
