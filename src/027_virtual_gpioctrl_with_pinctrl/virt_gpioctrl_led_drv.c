#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/cdev.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/consumer.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kline <kline_code@example,com>");
MODULE_DESCRIPTION("Test for virtual gpio controller");

#define DEBUG_LOG(fmt, ...) \
        printk(KERN_INFO "[%s line%d] %s(): "fmt"\n", \
            KBUILD_BASENAME, __LINE__, __FUNCTION__, ##__VA_ARGS__)


struct virt_led_drv {
    const char *label;
    struct device *dev;
    dev_t devno;
    struct class *cls;
    struct device *chrdev; // 根据
    struct gpio_desc *gpiod;
    struct cdev		cdev;
};

static struct virt_led_drv *g_virt_led = NULL;

static int virt_led_drv_open(struct inode *inode, struct file *file)
{
    int ret = 0;
    struct virt_led_drv *virt_led = container_of(inode->i_cdev, struct virt_led_drv, cdev);
    int minor = iminor(inode);

    DEBUG_LOG("Enter with minor: %d", minor);
    if (!virt_led) {
        DEBUG_LOG("Cannot get virtual led, use global pointer!");
        ret = gpiod_direction_output(g_virt_led->gpiod, 1);
    } else {
        DEBUG_LOG("Get virtual led success!");
        file->private_data = virt_led;
        ret = gpiod_direction_output(virt_led->gpiod, 1);
    }
    return ret;
}

static ssize_t virt_led_drv_write(struct file *file, const char __user *buf, size_t size, loff_t *offset)
{
    int val = 0;
    struct inode *inode = file_inode(file);
    struct virt_led_drv *virt_led = file->private_data;
    int minor = iminor(inode);

    DEBUG_LOG("Enter with minor: %d", minor);
    copy_from_user(&val, buf, 1);

    if (!virt_led) {
        DEBUG_LOG("Cannot get virtual led, use global pointer!");
        gpiod_set_value(g_virt_led->gpiod, val);
    } else {
        DEBUG_LOG("Get virtual led success!");
        gpiod_set_value(virt_led->gpiod, val);
    }

    DEBUG_LOG("Control LED %s!", val ? "ON" : "OFF");
    return 1;
}

static ssize_t virt_led_drv_read(struct file *file, char __user *buf, size_t size, loff_t *offset)
{
    int val = 0;
    struct virt_led_drv *virt_led = file->private_data;
    struct inode *inode = file_inode(file);
    int minor = iminor(inode);

    DEBUG_LOG("Enter with minor: %d", minor);

    if (!virt_led) {
        DEBUG_LOG("Cannot get virtual led, use global pointer!");
        val = gpiod_get_value(g_virt_led->gpiod);
    } else {
        DEBUG_LOG("Get virtual led success!");
        val = gpiod_get_value(virt_led->gpiod);
    }
    if (val < 0) {
        DEBUG_LOG("Get value via gpios failed!");
        return -1;
    }

    DEBUG_LOG("LED current status: %s", val ? "ON" : "OFF");
    copy_to_user(buf, &val, 1);
    return 1;
}

static long virt_led_drv_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct inode *inode = file_inode(file);
    int minor = iminor(inode);

    DEBUG_LOG("Enter with minor: %d", minor);
    return 1;
}

static int virt_led_drv_close(struct inode *inode, struct file *file)
{
    int minor = iminor(inode);

    DEBUG_LOG("Enter with minor: %d", minor);
    return 0;
}


static struct file_operations virt_led_drv_fop = {
    .owner =            THIS_MODULE,
    .open =             virt_led_drv_open,
    .read =             virt_led_drv_read,
    .write =            virt_led_drv_write,
    .unlocked_ioctl =   virt_led_drv_ioctl,
    .release =          virt_led_drv_close,
};

int virt_led_probe(struct platform_device *pdev)
{
    const char *label = NULL;
    struct device *dev = &pdev->dev;
    struct device_node *np = dev->of_node;

    DEBUG_LOG("Enter!");
    if (!dev || !np) {
        DEBUG_LOG("Invalid device node!");
        return -EINVAL;
    }

    g_virt_led = devm_kzalloc(dev, sizeof(*g_virt_led), GFP_KERNEL);
    if (!g_virt_led) {
        DEBUG_LOG("Alloc virtual led memory failed!");
        return -EINVAL;
    }
    
    // 从设备树中获取gpio数量，label
    if (of_property_read_string(np, "label", &label)) {
        DEBUG_LOG("Read data from dts failed!");
        return -ENOMEM;
    }

    g_virt_led->gpiod = devm_gpiod_get(dev, "led", GPIOD_OUT_LOW);
    if (!g_virt_led->gpiod) {
        DEBUG_LOG("Get gpio desc from dts failed!");
        return -EINVAL;
    }

    DEBUG_LOG("Get node name: %s, label: %s", np->name, label);

    // 字符设备驱动程序注册
    if (alloc_chrdev_region(&g_virt_led->devno, 0, 1, "virt_led_drv")) {
        DEBUG_LOG("Register chrdev region failed!");
        return -EINVAL;
    }

    cdev_init(&g_virt_led->cdev, &virt_led_drv_fop);
    g_virt_led->cdev.owner = THIS_MODULE;

	if (cdev_add(&g_virt_led->cdev, g_virt_led->devno, 1)) {
        DEBUG_LOG("cdev_add error!");
		return -EBUSY;
	}

    g_virt_led->cls = class_create(THIS_MODULE, "virt_led");
    if (!g_virt_led->cls) {
        DEBUG_LOG("Create class failed!");
        return -EINVAL;
    }

    g_virt_led->chrdev = device_create(g_virt_led->cls,
                            dev, 
                            g_virt_led->devno,
                            g_virt_led, 
                            "virt_led");
    if (!g_virt_led->chrdev) {
        DEBUG_LOG("Create char device node failed!");
        return -EINVAL;
    }

    DEBUG_LOG("Probe end!");
    return 0;
}


int virt_led_remove(struct platform_device *pdev)
{
    DEBUG_LOG("Enter!");

    device_destroy(g_virt_led->cls, g_virt_led->devno);

    class_destroy(g_virt_led->cls);

    // include\linux\kdev_t.h
    unregister_chrdev(MAJOR(g_virt_led->devno), "virt_led_drv");

    DEBUG_LOG("Remove end!");
    return 0;
}

static const struct of_device_id virt_led_of_match[] = {
    { .compatible = "100ask,virt_led" },
    {},
};

static struct platform_driver virt_led_driver = {
    .probe		= virt_led_probe,
    .remove		= virt_led_remove,
    .driver		= {
        .name	= "100ask_virt_led",
        .of_match_table = virt_led_of_match,
    },
};

module_platform_driver(virt_led_driver);
