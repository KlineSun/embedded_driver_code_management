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
#include <linux/interrupt.h>
#include "virt_i2c_drv.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kline <kline_code@example,com>");
MODULE_DESCRIPTION("Test for ts input device");



int local_virt_i2c_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    struct device_node *np = dev->of_node;
    DEBUG_LOG("Enter!");

    if (!np) {
        DEBUG_LOG("Invalid device node!");
        return -1;
    }


    DEBUG_LOG("Probe end!");
    return 0;
}


int local_virt_i2c_remove(struct platform_device *pdev)
{
    DEBUG_LOG("Enter!");

    return 0;
}

static const struct of_device_id local_virt_i2c_of_match[] = {
    { .compatible = "st,100ask_virt_i2c" },
    {},
};

static struct platform_driver local_virt_i2c_driver = {
    .probe		= local_virt_i2c_probe,
    .remove		= local_virt_i2c_remove,
    .driver		= {
        .name	= "local_virt_i2c",
        .of_match_table = local_virt_i2c_of_match,
    },
};

module_platform_driver(local_virt_i2c_driver);
