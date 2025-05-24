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
#include <linux/interrupt.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include "button_irq_drv.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kline <kline_code@example,com>");
MODULE_DESCRIPTION("Test for button bus driver");

struct local_button {
    int btn_num;
    int gpio_num;
    int irq;
    enum of_gpio_flags flags;
    char label[32];
    struct gpio_desc *gpiod;
};

static int major = 0;
static struct class *button_drv_class = NULL;
static struct local_button *g_btn_list = NULL;
static int btn_irq_cnt = 0;

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
    return 0;
}

static ssize_t button_drv_write(struct file *file, const char __user *buf, size_t size, loff_t *offset)
{
    char bit_val = 0;
    struct inode *inode = file_inode(file);
    int minor = iminor(inode);

    DEBUG_LOG("Enter with minor: %d", minor);
    copy_from_user(&bit_val, buf, 1);

    return 1;
}

static ssize_t button_drv_read(struct file *file, char __user *buf, size_t size, loff_t *offset)
{
    int val = 0;
    struct inode *inode = file_inode(file);
    int minor = iminor(inode);

    DEBUG_LOG("Enter with minor: %d", minor);
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

static irqreturn_t button_irq_cb(int irqno, void *priv_data)
{
	struct device_node *node = priv_data;
    const char *label = NULL;
    int i = 0;
    u32 num;

    DEBUG_LOG("Enter!");
    if (!node) {
        DEBUG_LOG("Invalid private data!");
        return IRQ_NONE;
    }

    of_property_read_string(node, "label", &label);
    of_property_read_u32(node, "num", &num);
    for (i = 0; i < btn_irq_cnt; i++) {
        if (!strcmp(label, g_btn_list[i].label) && num == g_btn_list[i].btn_num) {
            DEBUG_LOG("Match button%d", i);
            break;
        }
    }

    if (i >= btn_irq_cnt) {
        DEBUG_LOG("Mismatch button: %d", i);
        return IRQ_NONE;
    }

    DEBUG_LOG("trigger success: button%d, gpio%d, label<%s>, irq%d, flags%d",
        g_btn_list[i].btn_num,
        g_btn_list[i].gpio_num,
        g_btn_list[i].label,
        g_btn_list[i].irq,
        g_btn_list[i].flags);

	return IRQ_HANDLED;
}

int stm32mp157_button_probe(struct platform_device *pdev)
{
    struct device_node	*pn = NULL, *chid = NULL;
    int err = 0, idx = 0, ret = 0;
    if (!pdev) {
        DEBUG_LOG("Invalid platform device!");
        return -1;
    }

    pn = pdev->dev.of_node;
    if (!pn) {
        DEBUG_LOG("Cannot get device node from platform device!");
        return -1;
    }
    DEBUG_LOG("get device_node: name=%s, path=%s, id=%d", pn->name, pn->full_name, pn->phandle);

    /* 注册字符设备节点和class，以及file_operrations结构体 -begin */
    print_usage();
    major = register_chrdev(0, "button_irq_drv", &button_drv_fop);
    if (major < 0) {
        DEBUG_LOG("register button char device failed: %d", major);
        return major;
    }
    DEBUG_LOG("get device major num: %d", major);
	button_drv_class = class_create(THIS_MODULE, "button_irq_drv");
	if (IS_ERR(button_drv_class)) {
        DEBUG_LOG("Create button drv class failed!");
        unregister_chrdev(major, "button_irq_drv");
        return PTR_ERR(button_drv_class);
    }
    /* 注册字符设备节点和class，以及file_operrations结构体 -end */

    btn_irq_cnt = of_get_child_count(pn);
    if (btn_irq_cnt <= 0) {
        DEBUG_LOG("get button count failed!");
        err = -1;
        goto probe_err;
    }

    g_btn_list = kzalloc(sizeof(struct local_button) * btn_irq_cnt, GFP_KERNEL);
    if (!g_btn_list) {
        DEBUG_LOG("alloc kernel memory faield!");
        err = -2;
        goto probe_err;
    }

    for_each_child_of_node(pn, chid) {
        const char *label;
        struct device *button_drv_device = NULL;
        of_property_read_string(chid, "label", &label);
        of_property_read_u32(chid, "num", &g_btn_list[idx].btn_num);
        snprintf(g_btn_list[idx].label, 32, "%s", label);

        // get flags
        g_btn_list[idx].gpio_num = of_get_named_gpio_flags(chid,
                                                          "button-gpios",
                                                          0,
                                                          &g_btn_list[idx].flags);
        if (g_btn_list[idx].gpio_num < 0) {
            DEBUG_LOG("Failed to get gpio flags: %d", g_btn_list[idx].gpio_num);
            continue;
        }

        // get gpio desc
        g_btn_list[idx].gpiod = gpio_to_desc(g_btn_list[idx].gpio_num);
        if (!g_btn_list[idx].gpiod) {
            DEBUG_LOG("Failed to get gpio desc!");
            continue;
        }

        // get irq number
        g_btn_list[idx].irq = gpiod_to_irq(g_btn_list[idx].gpiod);
        if (g_btn_list[idx].irq <= 0) {
            DEBUG_LOG("Failed to get gpio irq: %d", g_btn_list[idx].irq);
            continue;
        }

        // set gpio input mode
        ret = gpio_direction_input(g_btn_list[idx].gpio_num);
        if (ret < 0) {
            DEBUG_LOG("Failed to set GPIO input: %d", ret);
            continue;
        }

        // request interrupt
        ret = request_irq(g_btn_list[idx].irq, button_irq_cb, IRQF_TRIGGER_FALLING, g_btn_list[idx].label, chid);
        if (ret) {
            DEBUG_LOG("request irq for %s failed: %d", g_btn_list[idx].label, ret);
            continue;
        }

        button_drv_device = device_create(button_drv_class,
                                        NULL,
                                        MKDEV(major, idx),
                                        NULL, "%s_%d", pn->name, idx);
        if (IS_ERR(button_drv_device)) {
            DEBUG_LOG("Create device failed, minor: %d", idx);
            continue;
        }
        DEBUG_LOG("probe success: button%d, gpio%d, label<%s>, irq%d, flags%d",
                                                                g_btn_list[idx].btn_num,
                                                                g_btn_list[idx].gpio_num,
                                                                g_btn_list[idx].label,
                                                                g_btn_list[idx].irq,
                                                                g_btn_list[idx].flags);
        idx++;
    }

    if (idx != btn_irq_cnt) {
        DEBUG_LOG("someone device_node probe failed: %d", idx);
        err = -3;
        goto probe_err;
    }
    return 0;

probe_err:
    class_destroy(button_drv_class);
    unregister_chrdev(major, "button_irq_drv");
    return err;
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
    for (i = 0; i < btn_irq_cnt; i++) {
        DEBUG_LOG("realase device node %d", i);
        device_destroy(button_drv_class, MKDEV(major, i));
    }

    class_destroy(button_drv_class);
	unregister_chrdev(major, "button_irq_drv");
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