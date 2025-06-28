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
#include "stm32mp157_input_drv.h"


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kline <kline_code@example,com>");
MODULE_DESCRIPTION("Test for ts input device");

struct stm32_input_dev {
    char label[32];
    unsigned int irq;
    struct input_dev *input;
};

static struct stm32_input_dev *g_stm32_input = NULL;

static irqreturn_t local_input_dev_isr(int irq, void *dev_id)
{
    DEBUG_LOG("Enter!");

    // get data from hardware
    DEBUG_LOG("get data from hardware!");

    // repot input event
    DEBUG_LOG("input event!");

    // retport sync event
    DEBUG_LOG("sync event!");

    return 0;
}

int local_input_dev_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    struct device_node *np = dev->of_node;
    const char *label = NULL;
    struct input_dev *input = NULL;
    DEBUG_LOG("Enter!");

    if (!np) {
        DEBUG_LOG("Invalid device node!");
        return -1;
    }

    g_stm32_input = devm_kzalloc(dev, sizeof(*g_stm32_input), GFP_KERNEL);
    if (!g_stm32_input) {
        DEBUG_LOG("Alloc stm32mp157 input device failed!");
        return -1;
    }

    // getd data from dts
    if (of_property_read_string(np, "label", &label) < 0) {
        DEBUG_LOG("Read property label from dts failed!");
        return -1;
    }
    strncpy(g_stm32_input->label, label, 32);

    // alloc input_dev、set、register
    input = devm_input_allocate_device(dev);
    if (!input) {
        DEBUG_LOG("Alloc input device failed!");
        return -1;
    }
    g_stm32_input->input = input;

    input->name = "100ask_input_dev";
    input->phys = "input/ts";
    input->id.bustype = BUS_VIRTUAL;
    input->id.vendor = 0x100A;
    input->id.product = 0x0001;
    input->id.version = 0x0100;

    // set key、code
    __set_bit(EV_KEY, input->evbit);
    __set_bit(EV_ABS, input->evbit);
    __set_bit(EV_SYN, input->evbit);

    __set_bit(BTN_TOUCH, input->keybit);
    __set_bit(ABS_MT_SLOT, input->absbit);
    __set_bit(ABS_MT_POSITION_X, input->absbit);
    __set_bit(ABS_MT_POSITION_Y, input->absbit);

    input_set_abs_params(input, ABS_MT_POSITION_X, 0, 1024, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_Y, 0, 600, 0, 0);
	input_set_abs_params(input, ABS_MT_PRESSURE, 0, 0xff, 0, 0);

    if (input_register_device(input)) {
        DEBUG_LOG("Failed to register input device!");
        return -1;
    }

    // get irq
    g_stm32_input->irq = platform_get_irq(pdev, 0);
    if (g_stm32_input->irq < 0) {
        DEBUG_LOG("Get interrupts from dts failed!");
        return -1;
    }
    
    if (devm_request_irq(dev, g_stm32_input->irq, local_input_dev_isr, 0, "stm32mp157_ts_irq", NULL) < 0) {
        DEBUG_LOG("Request interrupts failed!");
        return -1;
    }

    DEBUG_LOG("Probe end!");
    return 0;
}


int local_input_dev_remove(struct platform_device *pdev)
{
    DEBUG_LOG("Enter!");

    return 0;
}

static const struct of_device_id local_input_dev_of_match[] = {
    { .compatible = "100ask,ts_dev" },
    {},
};

static struct platform_driver local_input_dev_driver = {
    .probe		= local_input_dev_probe,
    .remove		= local_input_dev_remove,
    .driver		= {
        .name	= "local_input_dev",
        .of_match_table = local_input_dev_of_match,
    },
};

module_platform_driver(local_input_dev_driver);
