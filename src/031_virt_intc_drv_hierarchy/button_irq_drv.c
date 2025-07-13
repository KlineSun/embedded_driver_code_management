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
#include <linux/of_irq.h>
#include <linux/interrupt.h>
#include <linux/of_irq.h>
#include <linux/cdev.h>
#include <linux/platform_device.h>
#include <dt-bindings/gpio/gpio.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kline <kline_code@example,com>");
MODULE_DESCRIPTION("Test for button bus driver");


#define DEBUG_LOG(fmt, ...) \
        printk(KERN_INFO "[%s line%d] %s(): "fmt"\n", \
            KBUILD_BASENAME, __LINE__, __FUNCTION__, ##__VA_ARGS__)

struct gpio_button_desc {
    char name[32];
    int gpio;
    struct gpio_desc *gpiod;
    int irq;
    enum of_gpio_flags flag;
    int minor;
    struct device *chrdev;
};

struct dtb_button_device {
    int major;
    struct cdev cdev;
    struct class *cls;
    struct device *dev;
    const char *label;
    struct gpio_button_desc *btns;
    int nbtns;
};

static struct dtb_button_device *g_btn_descs = NULL;

static int button_drv_open(struct inode *inode, struct file *file)
{
    struct dtb_button_device *btn_dev = container_of(inode->i_cdev, struct dtb_button_device, cdev);
    int minor = iminor(inode);

    if (!btn_dev || minor >= g_btn_descs->nbtns) {
        DEBUG_LOG("Invalid file parameter!");
        return -1;
    }

    file->private_data = btn_dev;
    DEBUG_LOG("Open with minor: %d", minor);

    return 0;
}

static ssize_t button_drv_write(struct file *file, const char __user *buf, size_t size, loff_t *offset)
{
    char bit_val = 0;
    struct inode *inode = file_inode(file);
    int minor = iminor(inode);

    DEBUG_LOG("Enter with minor: %d", minor);
    copy_from_user(&bit_val, buf, 1);

    DEBUG_LOG("Readonly device, write operation is forbided!");
    return -EPERM;
}

static ssize_t button_drv_read(struct file *file, char __user *buf, size_t size, loff_t *offset)
{
    int val = 0;
    struct inode *inode = file_inode(file);
    int minor = iminor(inode);

    if (minor >= g_btn_descs->nbtns) {
        DEBUG_LOG("Invalid file parameter!");
        return -1;
    }

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

static irqreturn_t ask100_button_isr(int irqno, void *priv_data)
{
	struct gpio_button_desc *desc = priv_data;
    
    if (!desc) {
        DEBUG_LOG("Invalid private data!");
        return IRQ_NONE;
    }

    DEBUG_LOG("irq %d trigger!", desc->irq);
	return IRQ_HANDLED;
}

static struct dtb_button_device *alloc_button_res(int nirqs)
{
    struct dtb_button_device *btn_dev = NULL;

    DEBUG_LOG("Enter with %d irqs!", nirqs);
    btn_dev = kzalloc(sizeof(struct dtb_button_device), GFP_KERNEL);
    if (!btn_dev) {
        DEBUG_LOG("Alloc dtb_button_device memory failed!");
        return NULL;
    }

    btn_dev->btns = kcalloc(nirqs, sizeof(struct gpio_button_desc), GFP_KERNEL);
    if (!btn_dev->btns) {
        DEBUG_LOG("Alloc gpio_button_desc memory failed!");
        kfree(btn_dev);
        return NULL;
    }
    btn_dev->nbtns = nirqs;

    DEBUG_LOG("Alloc success!");
    return btn_dev;
}


static void free_button_res(struct dtb_button_device *dev)
{
    if (!dev) {
        DEBUG_LOG("Invalid parameter!");
        return;
    }
    DEBUG_LOG("Enter!");
    // free gpio_button_desc first
    if (dev->btns)
        kfree(dev->btns);

    // free dtb_button_device
    if (dev)
        kfree(dev);
}

int stm32mp157_button_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    struct device_node	*pn = pdev->dev.of_node;
    int irq_cnt = 0, i = 0, err = 0;
    dev_t devno;

    if (!dev || !pn) {
        DEBUG_LOG("Invalid device node!");
        return -EINVAL;
    }

    // 获取irq数量
    DEBUG_LOG("Enter!");
    /**
     * 这里会调用到使用的中断控制器的irq_domain的translate函数和alloc函数，
     * 如果未设置正确，此处的结构为0
    */
    irq_cnt = platform_irq_count(pdev);
    DEBUG_LOG("Enter with %d irqs!", irq_cnt);

    // 按照数量分配全局button资源
    g_btn_descs = alloc_button_res(irq_cnt);
    if (!g_btn_descs) {
        DEBUG_LOG("Alloc button resources failed!");
        return -ENOMEM;
    }

    // 字符设备初始化
    err = alloc_chrdev_region(&devno, 0, 1, "irq_button_drv");
    if (err) {
        DEBUG_LOG("Alloc button chrdev failed!");
        err = -EINVAL;
        goto btn_res_free;
    }
    g_btn_descs->major = MAJOR(devno);

    cdev_init(&g_btn_descs->cdev, &button_drv_fop);

    if (cdev_add(&g_btn_descs->cdev, MKDEV(g_btn_descs->major, 0), irq_cnt)) {
        DEBUG_LOG("Add cdev failed!");
        err = -EINVAL;
        goto chrdev_free;
    }

    // class创建
    g_btn_descs->cls = class_create(THIS_MODULE, "irq_button");
    if (IS_ERR(g_btn_descs->cls)) {
        DEBUG_LOG("Create class failed!");
        err = -EINVAL;
        goto cdev_free;
    }

    // 遍历gpio
    for (i = 0; i < irq_cnt; i++) {
        // 按键命名
        snprintf(g_btn_descs->btns[i].name, 32, "100ask_button_%d", i);

        // 获取中断资源
        g_btn_descs->btns[i].irq = of_irq_get(pn, i);
        if (g_btn_descs->btns[i].irq < 0) {
            DEBUG_LOG("Get irq resources failed!");
            err = g_btn_descs->btns[i].irq;
            if (i)
                goto dev_free;
            else
                goto class_free;
        }
        DEBUG_LOG("Found irq=%d, button_name=%s", g_btn_descs->btns[i].irq,
                                                  g_btn_descs->btns[i].name);

        // 每个gpio逐个申请中断，传入对应的gpio_button_desc作为参数
        err = request_irq(g_btn_descs->btns[i].irq, ask100_button_isr, IRQF_SHARED,
                        g_btn_descs->btns[i].name, &(g_btn_descs->btns[i]));
        if (err) {
            DEBUG_LOG("Request button irq failed!");
            if (i)
                goto dev_free;
            else
                goto class_free;
        }

        // 创建字符设备节点，传入g_btn_descs作为参数
        g_btn_descs->btns[i].chrdev = device_create(g_btn_descs->cls, 
                                                    dev,
                                                    MKDEV(g_btn_descs->major, i),
                                                    g_btn_descs,
                                                    "irq_button%d", i);
        if (IS_ERR(g_btn_descs->btns[i].chrdev)) {
            err = -EINVAL;
            DEBUG_LOG("Create button device failed!");
            if (i)
                goto irq_free;
            else
                goto class_free;
        }
        g_btn_descs->btns[i].minor = i;
    }

    DEBUG_LOG("Probe button success!");
    return 0;

dev_free:
    for (i = 0; i < irq_cnt; i++) {
        if (g_btn_descs->btns[i].chrdev)
            device_destroy(g_btn_descs->cls, MKDEV(g_btn_descs->major, i));
    }

irq_free:
    for (i = 0; i < irq_cnt; i++) {
        if (g_btn_descs->btns[i].irq > 0)
            free_irq(g_btn_descs->btns[i].irq, &g_btn_descs->btns[i]);
    }

class_free:
    class_destroy(g_btn_descs->cls);

cdev_free:
    cdev_del(&g_btn_descs->cdev);

chrdev_free:
    unregister_chrdev_region(devno, irq_cnt);

btn_res_free:
    if (g_btn_descs)
        free_button_res(g_btn_descs);

    return err;
}

int stm32mp157_button_remove(struct platform_device *pdev)
{
    int i =0;
    DEBUG_LOG("Enter!");
    if (!pdev) {
        DEBUG_LOG("Invalid platform device!");
        return -1;
    }

    for (i = 0; i < g_btn_descs->nbtns; i++) {
        // 逐个释放中断
        free_irq(g_btn_descs->btns[i].irq, &g_btn_descs->btns[i]);

        // 销毁device
        device_destroy(g_btn_descs->cls, MKDEV(g_btn_descs->major, i));
    }

    // 销毁class
    class_destroy(g_btn_descs->cls);

    // cdev删除
    cdev_del(&g_btn_descs->cdev);

    // 销毁字符设备驱动程序
    unregister_chrdev(g_btn_descs->major, "irq_button_drv");

    // 释放全局button资源
    if (g_btn_descs)
        free_button_res(g_btn_descs);

    g_btn_descs = NULL;
    DEBUG_LOG("End!");
    return 0;
}

static const struct of_device_id ask100_button_of_match[] = {
	{ .compatible = "100ask,virt_button_drv", },
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