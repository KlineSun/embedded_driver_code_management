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
#include "led_drv.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kline <kline_code@example,com>");
MODULE_DESCRIPTION("Test for led simple driver");

static int major = 0;
static struct class *led_drv_class = NULL;
static struct led_operations *g_led_oprts = NULL;

static int led_drv_open(struct inode *inode, struct file *file)
{
    int minor = iminor(inode);
    DEBUG_LOG("Enter with minor: %d", minor);

    if (g_led_oprts->config(minor)) {
        DEBUG_LOG("config led failed!");
        return -1;
    }

    return 0;
}

static ssize_t led_drv_read(struct file *file, char __user *buf, size_t size, loff_t *offset)
{
    char status = 0;
    struct inode *inode = file_inode(file);
    int minor = iminor(inode);

    DEBUG_LOG("Enter with minor: %d", minor);
    if (g_led_oprts->check(minor, &status, 1)) {
        DEBUG_LOG("check led status failed!");
        return -1;
    }

    DEBUG_LOG("led current status: %s", status == LED_ON ? "ON" : "OFF");
    copy_to_user(buf, &status, 1);
    return 1;
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
        if (g_led_oprts->ctrl(minor, LED_ON)) {
            DEBUG_LOG("control led on failed!");
            return -1;
        }
    } else {
        // led off
        if (g_led_oprts->ctrl(minor, LED_OFF)) {
            DEBUG_LOG("control led off failed!");
            return -1;
        }
    }
    DEBUG_LOG("Control led success!");
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
    int i = 0, j = 0, err = 0;
    struct device *led_drv_device;

    DEBUG_LOG("Enter!");
    // register
    major = register_chrdev(0, "led_frame_drv", &led_drv_fop);
    if (major < 0) {
        DEBUG_LOG("register led char device failed: %d", major);
        return major;
    }
    DEBUG_LOG("get device major num: %d", major);
    // class create
	led_drv_class = class_create(THIS_MODULE, "led_frame_drv");
	if (IS_ERR(led_drv_class)) {
        err = PTR_ERR(led_drv_class);
        DEBUG_LOG("Create led drv class failed!");
        goto chr_dev_release;
    }

    // get board operations
    g_led_oprts = get_led_oprts();
    if (!g_led_oprts) {
        DEBUG_LOG("Get led operations failed!");
        goto dev_release; 
    }

    for (i = 0; i < g_led_oprts->num; i++) {
        led_drv_device = device_create(led_drv_class, NULL, MKDEV(major, i), NULL, "led_frame_drv%d", i);
        if (IS_ERR(led_drv_device)) {
            err = PTR_ERR(led_drv_device);
            DEBUG_LOG("Create device failed, mimnor: %d", i);
            goto dev_release;
        }
    }

    // call init
    err = g_led_oprts->init();
    if (err) {
        DEBUG_LOG("init led failed!");
        goto dev_release;
    }

    DEBUG_LOG("Init led drv success!");
    return 0;

dev_release:
    DEBUG_LOG("%d devices need to be released!", i);
    if (i > 0) {
        for (j = 0; j < i; j++) {
            device_destroy(led_drv_class, MKDEV(major, j)); 
        }
    }

    if (led_drv_class) {
        class_destroy(led_drv_class);
    }

chr_dev_release:
    if (major >=0 ) {
        unregister_chrdev(major, "led_frame_drv");
    }
    return err;
}

static void __exit led_drv_exit(void)
{
    int i = 0;
    DEBUG_LOG("Enter!");

    if (g_led_oprts) {
        g_led_oprts->destroy();
    } else {
        DEBUG_LOG("led operations missing!");
    }

    for (i = 0; i < g_led_oprts->num; i++) {
        device_destroy(led_drv_class, MKDEV(major, i));
    }
    class_destroy(led_drv_class);
	unregister_chrdev(major, "led_frame_drv");
}

module_init(led_drv_init);
module_exit(led_drv_exit);
