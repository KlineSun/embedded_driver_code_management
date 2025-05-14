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
#include "button_base_drv.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kline <kline_code@example,com>");
MODULE_DESCRIPTION("Test for button bus driver");

static int major = 0;
static struct class *button_drv_class = NULL;
static struct button_operations *g_button_ops = NULL;

static void print_usage(void)
{
    DEBUG_LOG("Usage:");
    DEBUG_LOG("insmod sequence: base_module  -> chip_module -> dtb replace");
    DEBUG_LOG("rmmod sequence:  dtb replace  -> chip_module -> base_module");
}

static int button_drv_open(struct inode *inode, struct file *file)
{
    int minor = iminor(inode);
    DEBUG_LOG("Enter with minor: %d", minor);

    if (g_button_ops->config(minor)) {
        DEBUG_LOG("config button failed!");
        return -1;
    }
    return 0;
}

static ssize_t button_drv_write(struct file *file, const char __user *buf, size_t size, loff_t *offset)
{
    char bit_val = 0;
    struct inode *inode = file_inode(file);
    int minor = iminor(inode);

    DEBUG_LOG("Enter with minor: %d", minor);
    copy_from_user(&bit_val, buf, 1);

    // button write暂无定义

    DEBUG_LOG("Control button success!");
    return 1;
}

static ssize_t button_drv_read(struct file *file, char __user *buf, size_t size, loff_t *offset)
{
    char status = 0;
    struct inode *inode = file_inode(file);
    int minor = iminor(inode);

    DEBUG_LOG("Enter with minor: %d", minor);
    if (g_button_ops->check(minor, &status, 1)) {
        DEBUG_LOG("check button status failed!");
        return -1;
    }

    DEBUG_LOG("button current status: %s", status == KEY_DOWN ? "DOWN" : "UP");
    copy_to_user(buf, &status, 1);
    return 1;
}

static long button_drv_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct inode *inode = file_inode(file);
    int minor = iminor(inode);

    DEBUG_LOG("Enter with minor: %d", minor);
    return 1;
}

static int button_drv_close(struct inode *inode, struct file *file)
{
    int minor = iminor(inode);

    DEBUG_LOG("Enter with minor: %d", minor);
    if (g_button_ops->release(minor)) {
        DEBUG_LOG("config button failed!");
        return -1;
    }
    return 0;
}

static struct file_operations button_drv_fop = {
    .owner =            THIS_MODULE,
    .open =             button_drv_open,
    .read =             button_drv_read,
    .write =            button_drv_write,
    .unlocked_ioctl =   button_drv_ioctl,
    .release =          button_drv_close,
};

static int __init button_drv_init(void)
{
    print_usage();
    DEBUG_LOG("Enter!");
    // register
    major = register_chrdev(0, "button_pdev_drv", &button_drv_fop);
    if (major < 0) {
        DEBUG_LOG("register button char device failed: %d", major);
        return major;
    }
    DEBUG_LOG("get device major num: %d", major);
    // class create
	button_drv_class = class_create(THIS_MODULE, "button_pdev_drv");
	if (IS_ERR(button_drv_class)) {
        DEBUG_LOG("Create button drv class failed!");
        unregister_chrdev(major, "button_pdev_drv");
        return PTR_ERR(button_drv_class);
    }
    DEBUG_LOG("Init button drv success!");
    return 0;
}

static void __exit button_drv_exit(void)
{
    if (g_button_ops != NULL) {
        DEBUG_LOG("platform button driver not removed yet, please remove first!");
        print_usage();
        return;
    }
    DEBUG_LOG("Enter!");
    class_destroy(button_drv_class);
	unregister_chrdev(major, "button_pdev_drv");
}

module_init(button_drv_init);
module_exit(button_drv_exit);


/*----------------------------------public api begin-----------------------------------------*/
int button_operations_register(struct button_operations *ops)
{
    if (!ops) {
        DEBUG_LOG("invalid operations!");
        return -1;
    }

    DEBUG_LOG("Enter!");
    if (g_button_ops == ops) {
        DEBUG_LOG("ops already reagister and init!");
    } else {
        if (g_button_ops) {
            DEBUG_LOG("Ops changed, destroy previous resource first!");
            g_button_ops->destroy();
        }

        g_button_ops = ops;
        if (g_button_ops->init()) {
            g_button_ops = NULL;
            DEBUG_LOG("init button failed, exit register!");
            return -1;
        }
    }

    return 0;
}

int button_operations_unregister(struct button_operations *ops)
{
    if (!ops || ops != g_button_ops) {
        DEBUG_LOG("invalid operations!");
        return -1;
    }

    DEBUG_LOG("Enter!");
    ops->destroy();
    g_button_ops = NULL;
    return 0;
}

int button_device_create(int minor)
{
    struct device *button_drv_device;
    if (!g_button_ops) {
        DEBUG_LOG("Not register button operations yet, please insmod chip-level module first!");
        print_usage();
        return -1;
    }
    DEBUG_LOG("Enter!");
    button_drv_device = device_create(button_drv_class, NULL, MKDEV(major, minor), NULL, "%s_%d", g_button_ops->dev_name, minor);
    if (IS_ERR(button_drv_device)) {
        DEBUG_LOG("Create device failed, mimnor: %d", minor);
        return PTR_ERR(button_drv_device);
    }
    return 0;
}

int button_device_destroy(int minor)
{
    if (!g_button_ops) {
        DEBUG_LOG("Not register button operations yet, please insmod chip-level module first!");
        return -1;
    }
    DEBUG_LOG("Enter!");
    device_destroy(button_drv_class, MKDEV(major, minor));
    return 0;
}

EXPORT_SYMBOL(button_operations_register);
EXPORT_SYMBOL(button_operations_unregister);
EXPORT_SYMBOL(button_device_create);
EXPORT_SYMBOL(button_device_destroy);
/*----------------------------------public api end-----------------------------------------*/
