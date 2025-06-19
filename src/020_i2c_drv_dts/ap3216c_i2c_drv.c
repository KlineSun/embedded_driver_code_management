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
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/mod_devicetable.h>
#include <linux/bitops.h>
#include <linux/jiffies.h>
#include <linux/property.h>
#include <linux/acpi.h>
#include <linux/of.h>
#include <linux/nvmem-provider.h>
#include <linux/regmap.h>
#include <linux/i2c.h>
#include <linux/pm_runtime.h>
#include <linux/i2c-dev.h>
#include <linux/cdev.h>
#include "ap3216c_i2c_drv.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kline <kline_code@example,com>");
MODULE_DESCRIPTION("Test for ap3216c i2c driver");


struct ap3216c_dev {
    struct cdev cdev;
    char name[I2C_NAME_SIZE];
    struct i2c_client *i2c_clnt;
};

static int major = 0;
static struct class *ap3216c_i2c_drv_class = NULL;

static int ap3216c_i2c_drv_open(struct inode *inode, struct file *file)
{
    int minor = iminor(inode);
    struct ap3216c_dev *dev = container_of(inode->i_cdev, struct ap3216c_dev, cdev);

    if (!dev || !dev->i2c_clnt) {
        DEBUG_LOG("Invalid i2c_client!");
        return -1;
    }

    DEBUG_LOG("Enter with minor: %d", minor);
    DEBUG_LOG("Device is on I2C bus '%s'\n", dev->i2c_clnt->adapter->name);
    DEBUG_LOG("I2C bus number: %d\n", dev->i2c_clnt->adapter->nr);

    /* 启动ap3216c */
    // 先复位所有寄存器
    if (i2c_smbus_write_byte_data(dev->i2c_clnt, 0, 0x04) < 0) {
        DEBUG_LOG("write data to ap3216c failed!");
        return -1;
    }
    mdelay(10);

    // ALS：往0x00写1，启动后的转化时间100ms
    // PS+IR：往0x00写2，启动后的转化时间12.5ms
    // ALS+PS+IR：往0x00写3，启动后的转化时间225ms
    if (i2c_smbus_write_byte_data(dev->i2c_clnt, 0, 0x03)) {
        DEBUG_LOG("write data to ap3216c failed!");
        return -1;
    }
    mdelay(225);
    // 单次ALS：往0x00写5，获取一次ALS数据后ap3216c自动停止，总耗时250ms
    // 单次PS+IR：往0x00写6，同上，总耗时2.5倍
    // 单次ALS+PS+IR：往0x00写7，同上，总耗时232ms

    file->private_data = dev;
    return 0;
}

static ssize_t ap3216c_i2c_drv_write(struct file *file, const char __user *buf, size_t size, loff_t *offset)
{
    char bit_val = 0;
    struct inode *inode = file_inode(file);
    int minor = iminor(inode);

    DEBUG_LOG("Enter with minor: %d", minor);
    copy_from_user(&bit_val, buf, 1);
    return 1;
}

static ssize_t ap3216c_i2c_drv_read(struct file *file, char __user *buf, size_t size, loff_t *offset)
{
    struct inode *inode = file_inode(file);
    int minor = iminor(inode);
    struct ap3216c_dev *dev = file->private_data;
    short ir_high = 0, ir_low = 0;
    short als_high = 0, als_low = 0;
    short ps_high = 0, ps_low = 0;
    short val[3] = {0};

    DEBUG_LOG("Enter with minor: %d", minor);

    /* 读取传感器数值 */
    // IR：低2位0x0a [1:0]，高8位0x0b
    ir_low = i2c_smbus_read_byte_data(dev->i2c_clnt, 0x0a);
    ir_high = i2c_smbus_read_byte_data(dev->i2c_clnt, 0x0b);
    if (ir_low < 0 || ir_high <0) {
        DEBUG_LOG("read data from ap3216c via i2c failed: %d, %d", ir_high, ir_low);
        return -1;
    }

    // ALS：低8位0x0c，高8位0x0d
    als_low = i2c_smbus_read_byte_data(dev->i2c_clnt, 0x0c);
    als_high = i2c_smbus_read_byte_data(dev->i2c_clnt, 0x0d);
    if (als_low < 0 || als_high <0) {
        DEBUG_LOG("read data from ap3216c via i2c failed: %d, %d", als_high, als_low);
        return -1;
    }

    // PS：低4位0x0e [3:0]，高6位0x0f [5:0]
    ps_low = i2c_smbus_read_byte_data(dev->i2c_clnt, 0x0e);
    ps_high = i2c_smbus_read_byte_data(dev->i2c_clnt, 0x0f);
    if (ps_low < 0 || ps_high <0) {
        DEBUG_LOG("read data from ap3216c via i2c failed: %d, %d", ps_high, ps_low);
        return -1;
    }

    val[0] = ((ir_high & 0xff) << 2) | (ir_low & 0b11);
    val[1] = ((als_high & 0xff) << 8) | (als_low & 0xff);
    val[2] = ((ps_high & 0xff) << 6) | (ps_low & 0b1111);

    DEBUG_LOG("AP3216C: IR=%d, ALS=%d, PS=%d", val[0], val[1], val[2]);
    copy_to_user(buf, val, sizeof(val));
    return sizeof(val);
}

static long ap3216c_i2c_drv_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct inode *inode = file_inode(file);
    int minor = iminor(inode);

    DEBUG_LOG("Enter with minor: %d", minor);
	return 0;
}

static int ap3216c_i2c_drv_close(struct inode *inode, struct file *file)
{
    int minor = iminor(inode);
    struct ap3216c_dev *dev = file->private_data;

    DEBUG_LOG("Enter with minor: %d", minor);
    // 复位ap3216c，往0x00写4，复位等待时间10ms
    if (i2c_smbus_write_byte_data(dev->i2c_clnt, 0, 0x04)) {
        DEBUG_LOG("write data to ap3216c failed!");
        return -1;
    }
    mdelay(10);

    // 关闭ap3216c：往0x00写0
    if (i2c_smbus_write_byte_data(dev->i2c_clnt, 0, 0)) {
        DEBUG_LOG("write data to ap3216c failed!");
        return -1;
    }
    return 0;
}

static struct file_operations ap3216c_i2c_drv_fop = {
    .owner =            THIS_MODULE,
    .open =             ap3216c_i2c_drv_open,
    .read =             ap3216c_i2c_drv_read,
    .write =            ap3216c_i2c_drv_write,
    .unlocked_ioctl =   ap3216c_i2c_drv_ioctl,
    .release =          ap3216c_i2c_drv_close,
};


static int ap3216c_i2c_probe(struct i2c_client *client)
{
    int err = 0;
    dev_t devno;
    struct device *ap3216c_i2c_drv_device;
    struct i2c_adapter *adapter = client->adapter;
    struct ap3216c_dev *dev = NULL;

    if (!adapter) {
        DEBUG_LOG("Invalid i2c adapter!");
        return -1;
    }

    DEBUG_LOG("Enter!");

    // alloc ap3216c device
    dev = devm_kzalloc(&client->dev, sizeof(struct ap3216c_dev), GFP_KERNEL);
    if (!dev) {
        DEBUG_LOG("Create mmap drv class failed!");
        err = -ENOMEM;
        goto class_free;
    }

    dev->i2c_clnt = client;
    strncpy(dev->name, client->name, I2C_NAME_SIZE);

    err = alloc_chrdev_region(&devno, 0, 1, "ap3216c_i2c_drv");
    if (err < 0) {
        DEBUG_LOG("Register chrdev failed!");
        return err;
    }
    major = MAJOR(devno);

    // 初始化cdev
    cdev_init(&dev->cdev, &ap3216c_i2c_drv_fop);
    dev->cdev.owner = THIS_MODULE;

    // 添加cdev到系统
    err = cdev_add(&dev->cdev, devno, 1);
    if (err) {
        DEBUG_LOG("Add chrdev failed!");
        unregister_chrdev_region(devno, 1);
        return err;
    }
    DEBUG_LOG("get device major num: %d", major);
    dev_set_drvdata(&client->dev, dev);

    // class create
	ap3216c_i2c_drv_class = class_create(THIS_MODULE, "ap3216c_i2c_drv");
	if (IS_ERR(ap3216c_i2c_drv_class)) {
        err = PTR_ERR(ap3216c_i2c_drv_class);
        DEBUG_LOG("Create mmap drv class failed!");
        goto chredv_free;
    }

    ap3216c_i2c_drv_device = device_create(ap3216c_i2c_drv_class, NULL, MKDEV(major, 0), NULL, "ap3216c_i2c_drv");
    if (IS_ERR(ap3216c_i2c_drv_device)) {
		DEBUG_LOG("Create mmap drv class failed!");
        err = PTR_ERR(ap3216c_i2c_drv_device);
        goto class_free;
	}
    return 0;

class_free:
    if (!ap3216c_i2c_drv_class)
        class_destroy(ap3216c_i2c_drv_class);

chredv_free:
    if (major > 0)
        unregister_chrdev(major, "ap3216c_i2c_drv");
    major = 0;
    return err;
}

static int ap3216c_i2c_remove(struct i2c_client *i2c)
{
    DEBUG_LOG("Enter!");

    device_destroy(ap3216c_i2c_drv_class, MKDEV(major, 0));

    class_destroy(ap3216c_i2c_drv_class);
	/*
	 * Unregister the character device interface to the driver.
	 */
	unregister_chrdev(major, "ap3216c_i2c_drv");

    return 0;
}

static const struct of_device_id ap3216c_i2c_of_match[] = {
    { .compatible = "lsc,ap3216c", },
    { },
};

static const struct i2c_device_id ap3216c_i2c_id[] = {
    { "ap3216c", 0 },
    { },
};

static struct i2c_driver ap3216c_i2c_driver = {
    .driver = {
        .name = "ap3216c",
        .of_match_table = ap3216c_i2c_of_match,
    },
    .probe_new = ap3216c_i2c_probe,
    .remove = ap3216c_i2c_remove,
    .id_table = ap3216c_i2c_id,
};


module_i2c_driver(ap3216c_i2c_driver);
