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
#include <asm/io.h>
#include "led_base_drv.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kline <kline_code@example,com>");
MODULE_DESCRIPTION("Test for led bus driver");

static int major = 0;
static struct class *led_drv_class = NULL;
static struct led_operations *g_led_ops = NULL;

static void print_usage(void)
{
    DEBUG_LOG("Usage:");
    DEBUG_LOG("insmod sequence: base_module  -> chip_module -> board_module");
    DEBUG_LOG("rmmod sequence:  board_module -> chip_module -> base_module");
}

static int led_drv_open(struct inode *inode, struct file *file)
{
    int minor = iminor(inode);
    DEBUG_LOG("Enter with minor: %d", minor);

    if (g_led_ops->config(minor)) {
        DEBUG_LOG("config led failed!");
        return -1;
    }
    return 0;
}

static ssize_t led_drv_write(struct file *file, const char __user *buf, size_t size, loff_t *offset)
{
    char bit_val = 0;
    struct inode *inode = file_inode(file);
    int minor = iminor(inode);

    DEBUG_LOG("Enter with minor: %d", minor);
    copy_from_user(&bit_val, buf, 1);

    if (bit_val) {
        // led on
        if (g_led_ops->ctrl(minor, LED_ON)) {
            DEBUG_LOG("control led on failed!");
            return -1;
        }
    } else {
        // led off
        if (g_led_ops->ctrl(minor, LED_OFF)) {
            DEBUG_LOG("control led off failed!");
            return -1;
        }
    }
    DEBUG_LOG("Control led success!");
    return 1;
}

static ssize_t led_drv_read(struct file *file, char __user *buf, size_t size, loff_t *offset)
{
    char status = 0;
    struct inode *inode = file_inode(file);
    int minor = iminor(inode);

    DEBUG_LOG("Enter with minor: %d", minor);
    if (g_led_ops->check(minor, &status, 1)) {
        DEBUG_LOG("check led status failed!");
        return -1;
    }

    DEBUG_LOG("led current status: %s", status == LED_ON ? "ON" : "OFF");
    copy_to_user(buf, &status, 1);
    return 1;
}

static long led_drv_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct inode *inode = file_inode(file);
    int minor = iminor(inode);

    DEBUG_LOG("Enter with minor: %d", minor);
    return 1;
}

static int led_drv_close(struct inode *inode, struct file *file)
{
    int minor = iminor(inode);

    DEBUG_LOG("Enter with minor: %d", minor);
    if (g_led_ops->release(minor)) {
        DEBUG_LOG("config led failed!");
        return -1;
    }
    return 0;
}

static struct file_operations led_drv_fop = {
    .owner =            THIS_MODULE,
    .open =             led_drv_open,
    .read =             led_drv_read,
    .write =            led_drv_write,
    .unlocked_ioctl =   led_drv_ioctl,
    .release =          led_drv_close,
};

static int __init led_drv_init(void)
{
    print_usage();
    DEBUG_LOG("Enter!");
    // register
    major = register_chrdev(0, "led_pdev_drv", &led_drv_fop);
    if (major < 0) {
        DEBUG_LOG("register led char device failed: %d", major);
        return major;
    }
    DEBUG_LOG("get device major num: %d", major);
    // class create
	led_drv_class = class_create(THIS_MODULE, "led_pdev_drv");
	if (IS_ERR(led_drv_class)) {
        DEBUG_LOG("Create led drv class failed!");
        unregister_chrdev(major, "led_pdev_drv");
        return PTR_ERR(led_drv_class);
    }
    DEBUG_LOG("Init led drv success!");
    return 0;
}

static void __exit led_drv_exit(void)
{
    if (g_led_ops != NULL) {
        DEBUG_LOG("platform led driver not removed yet, please remove first!");
        print_usage();
        return;
    }
    DEBUG_LOG("Enter!");
    class_destroy(led_drv_class);
	unregister_chrdev(major, "led_pdev_drv");
}

module_init(led_drv_init);
module_exit(led_drv_exit);


/*----------------------------------public api begin-----------------------------------------*/
int led_operations_register(struct led_operations *ops)
{
    if (!ops) {
        DEBUG_LOG("invalid operations!");
        return -1;
    }

    DEBUG_LOG("Enter!");
    if (g_led_ops == ops) {
        DEBUG_LOG("ops already reagister and init!");
    } else {
        if (g_led_ops) {
            DEBUG_LOG("Ops changed, destroy previous resource first!");
            g_led_ops->destroy();
        }

        g_led_ops = ops;
        if (g_led_ops->init()) {
            g_led_ops = NULL;
            DEBUG_LOG("init led failed, exit register!");
            return -1;
        }
    }

    return 0;
}

int led_operations_unregister(struct led_operations *ops)
{
    if (!ops || ops != g_led_ops) {
        DEBUG_LOG("invalid operations!");
        return -1;
    }

    DEBUG_LOG("Enter!");
    ops->destroy();
    g_led_ops = NULL;
    return 0;
}

int led_device_create(int minor)
{
    struct device *led_drv_device;
    if (!g_led_ops) {
        DEBUG_LOG("Not register led operations yet, please insmod chip-level module first!");
        print_usage();
        return -1;
    }
    DEBUG_LOG("Enter!");
    led_drv_device = device_create(led_drv_class, NULL, MKDEV(major, minor), NULL, "%s_%d", g_led_ops->dev_name, minor);
    if (IS_ERR(led_drv_device)) {
        DEBUG_LOG("Create device failed, mimnor: %d", minor);
        return PTR_ERR(led_drv_device);
    }
    return 0;
}

int led_device_destroy(int minor)
{
    if (!g_led_ops) {
        DEBUG_LOG("Not register led operations yet, please insmod chip-level module first!");
        return -1;
    }
    DEBUG_LOG("Enter!");
    device_destroy(led_drv_class, MKDEV(major, minor));
    return 0;
}

EXPORT_SYMBOL(led_operations_register);
EXPORT_SYMBOL(led_operations_unregister);
EXPORT_SYMBOL(led_device_create);
EXPORT_SYMBOL(led_device_destroy);
/*----------------------------------public api end-----------------------------------------*/
