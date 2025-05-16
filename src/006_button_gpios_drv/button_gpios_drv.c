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
#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include "button_gpios_drv.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kline <kline_code@example,com>");
MODULE_DESCRIPTION("Test for button bus driver");

static int major = 0;
static struct class *button_drv_class = NULL;

static struct gpio_desc *gpiod_list[MAX_GPIO_BUTN_NUM] = {0};
static int active_flag[MAX_GPIO_BUTN_NUM] = {0};
static int gpiod_cnt = 0;

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

    if (!gpiod_list[minor]) {
        DEBUG_LOG("Enter with minor: %d", minor);
        return -1;
    }

    // set input mode
    if (gpiod_direction_input(gpiod_list[minor])) {
        DEBUG_LOG("set input mode failed!");
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

    DEBUG_LOG("Control button success!");
    return 1;
}

static ssize_t button_drv_read(struct file *file, char __user *buf, size_t size, loff_t *offset)
{
    int val = 0;
    struct inode *inode = file_inode(file);
    int minor = iminor(inode);

    DEBUG_LOG("Enter with minor: %d", minor);

    val = gpiod_get_value(gpiod_list[minor]);
    if (val < 0) {
        DEBUG_LOG("Get value via gpios faield!");
        return -1;
    }

    DEBUG_LOG("button current status: %s", val == active_flag[minor] ? "DOWN" : "UP");
    copy_to_user(buf, &val, 1);
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


int stm32mp157_button_probe(struct platform_device *pdev)
{
    struct device_node	*pn = NULL, *chid = NULL;
    if (!pdev) {
        DEBUG_LOG("Invalid platform device!");
        return -1;
    }

    pn = pdev->dev.of_node;
    if (!pn) {
        DEBUG_LOG("Cannot get device node from platform device!");
        return -1;
    }

    DEBUG_LOG("get device node: name=%s, path=%s, id=%d", pn->name, pn->full_name, pn->phandle);

    /* 注册字符设备节点和class，以及file_operrations结构体 -begin */
    print_usage();
    major = register_chrdev(0, "button_pdev_drv", &button_drv_fop);
    if (major < 0) {
        DEBUG_LOG("register button char device failed: %d", major);
        return major;
    }
    DEBUG_LOG("get device major num: %d", major);
	button_drv_class = class_create(THIS_MODULE, "button_pdev_drv");
	if (IS_ERR(button_drv_class)) {
        DEBUG_LOG("Create button drv class failed!");
        unregister_chrdev(major, "button_pdev_drv");
        return PTR_ERR(button_drv_class);
    }
    /* 注册字符设备节点和class，以及file_operrations结构体 -end */

    // 根据子节点数目来创建设备节点
    for_each_child_of_node(pn, chid) {
        const char *label;
        struct device *button_drv_device = NULL;
        enum of_gpio_flags flags;
        int gpio_num = of_get_named_gpio_flags(chid, "button-gpios", 0, &flags);
        if (gpio_num < 0) {
            DEBUG_LOG("Failed to get button-gpios in node %s\n", chid->name);
            continue;
        }
        DEBUG_LOG("get gpio number: %d", gpio_num);

        gpiod_list[gpiod_cnt] = gpio_to_desc(gpio_num);
        if (!gpiod_list[gpiod_cnt]) {
            DEBUG_LOG("Invalid gpio descriptor for GPIO %d\n", gpio_num);
            continue;
        }

        of_property_read_string(chid, "label", &label);

        button_drv_device = device_create(button_drv_class,
                                        NULL,
                                        MKDEV(major, gpiod_cnt),
                                        NULL, "%s_%d", pn->name, gpiod_cnt);
        if (IS_ERR(button_drv_device)) {
            DEBUG_LOG("Create device failed, minor: %d", gpiod_cnt);
            continue;
        }

        if (flags & OF_GPIO_ACTIVE_LOW) {
            active_flag[gpiod_cnt] = 0;
        } else {
            active_flag[gpiod_cnt] = 1;
        }
        gpiod_cnt++;
        DEBUG_LOG("get gpio number: %d, labe=%s, count=%d", gpio_num, label, gpiod_cnt);
    }

    return 0;
}

int stm32mp157_button_remove(struct platform_device *pdev)
{
    int i = 0;
    struct device_node	*pn = NULL;
    DEBUG_LOG("Enter!");
    if (!pdev) {
        DEBUG_LOG("Invalid platform device!");
        return -1;
    }

    pn = pdev->dev.of_node;
    if (!pn) {
        DEBUG_LOG("Cannot get device node from platform device!");
        return -1;
    }

    // 逐个释放设备节点
    for (i = 0; i < gpiod_cnt; i++) {
        DEBUG_LOG("realase device node %d", i);
        device_destroy(button_drv_class, MKDEV(major, i));
    }

    class_destroy(button_drv_class);
	unregister_chrdev(major, "button_pdev_drv");
    return 0;
}

static const struct of_device_id ask100_button_of_match[] = {
	{ .compatible = "100ask,button_dtb_drv", },
	{},
};

static struct platform_driver stm32mp157_button_driver = {
	.probe		= stm32mp157_button_probe,
	.remove		= stm32mp157_button_remove,
	.driver		= {
		.name	= "100ask_button",
        .of_match_table = ask100_button_of_match,
	},
};

module_platform_driver(stm32mp157_button_driver);