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
#include <linux/gpio/driver.h>
#include "virt_gpioctrl_drv.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kline <kline_code@example,com>");
MODULE_DESCRIPTION("Test for virtual gpio controller");


struct virt_gpioctrl_chip {
    const char *label;
    struct device *dev;
    struct gpio_chip chip;
    struct gpio_desc *descs;
    /**
     * gpio pin脚状态的缓存buf
     * bit 0: 当前电平
     * bit 1: 当前模式，输入/输出
     * bit 2~3: 保留
     * bit 4~7: gpio flag，参考gpio_desc中flag的定义
     * 
    */
    u8 *cache_buf;
    unsigned int ngpio; // 当前gpio控制器有多少个gpio
};

static struct virt_gpioctrl_chip *g_virt_gpiochip = NULL;

int virt_gpioctrl_request(struct gpio_chip *chip, unsigned offset)
{
    struct virt_gpioctrl_chip *virt_chip = gpiochip_get_data(chip);
    if (!virt_chip || offset >= chip->ngpio) {
        DEBUG_LOG("Invalid parameters!");
        return -EINVAL;
    }

    DEBUG_LOG("Enter!");
    DEBUG_LOG("gpioctrl label: %s", virt_chip->label);
    if (!virt_chip->cache_buf || offset >= virt_chip->ngpio) {
        DEBUG_LOG("Invalid parameters!");
        return -EINVAL;
    }

    // 设置flag字段为0(FLAG_REQUESTED)
    DEBUG_LOG("Hardware init!");
    virt_chip->cache_buf[offset] &= ~(0xf << 4);
    DEBUG_LOG("End!");
    return 0;
}

void virt_gpioctrl_free(struct gpio_chip *chip, unsigned offset)
{
    struct virt_gpioctrl_chip *virt_chip = gpiochip_get_data(chip);
    if (!virt_chip || offset >= chip->ngpio) {
        DEBUG_LOG("Invalid parameters!");
        return;
    }

    DEBUG_LOG("Enter!");
    DEBUG_LOG("gpioctrl label: %s", virt_chip->label);
    if (!virt_chip->cache_buf || offset >= virt_chip->ngpio) {
        DEBUG_LOG("Invalid parameters!");
        return;
    }

    // 恢复flag字段
    DEBUG_LOG("Hardware restore!");
    virt_chip->cache_buf[offset] &= ~(0xf << 4);
    DEBUG_LOG("End!");
}

int virt_gpioctrl_get_direction(struct gpio_chip *chip, unsigned offset)
{
    int dirt = 0; // 0: input, 1: output
    struct virt_gpioctrl_chip *virt_chip = gpiochip_get_data(chip);
    if (!virt_chip || offset >= virt_chip->ngpio) {
        DEBUG_LOG("Invalid parameters!");
        return -EINVAL;
    }
    DEBUG_LOG("Enter!");

    // 检查buf第1位，表示方向
    dirt = virt_chip->cache_buf[offset] & (1 << 1) ? 1 : 0;
    DEBUG_LOG("gpio %d is %s direction!", offset, dirt ? "output" : "input");
    return dirt;
}

int virt_gpioctrl_direction_input(struct gpio_chip *chip, unsigned offset)
{
    struct virt_gpioctrl_chip *virt_chip = gpiochip_get_data(chip);
    if (!virt_chip || offset >= virt_chip->ngpio) {
        DEBUG_LOG("Invalid parameters!");
        return -EINVAL;
    }
    DEBUG_LOG("Enter!");

    virt_chip->cache_buf[offset] &= ~(1 << 1);
    DEBUG_LOG("%s set gpio%d as input!", virt_chip->label, offset);
    return 0;
}

int virt_gpioctrl_direction_output(struct gpio_chip *chip, unsigned offset, int value)
{
    struct virt_gpioctrl_chip *virt_chip = gpiochip_get_data(chip);
    if (!virt_chip || offset >= virt_chip->ngpio) {
        DEBUG_LOG("Invalid parameters!");
        return -EINVAL;
    }
    DEBUG_LOG("Enter!");

    virt_chip->cache_buf[offset] |= 1 << 1;
    DEBUG_LOG("%s set gpio%d as output!", virt_chip->label, offset);
    return 0;
}

int virt_gpioctrl_get(struct gpio_chip *chip, unsigned offset)
{
    int val = 0;
    struct virt_gpioctrl_chip *virt_chip = gpiochip_get_data(chip);
    if (!virt_chip || offset >= virt_chip->ngpio) {
        DEBUG_LOG("Invalid parameters!");
        return -EINVAL;
    }
    DEBUG_LOG("Enter!");

    val = virt_chip->cache_buf[offset] & 0x01;
    DEBUG_LOG("%s get gpio%d status: %s", virt_chip->label, offset, val ? "high" : "low");
    return val;
}

void virt_gpioctrl_set(struct gpio_chip *chip, unsigned offset, int value)
{
    struct virt_gpioctrl_chip *virt_chip = gpiochip_get_data(chip);
    if (!virt_chip || offset >= virt_chip->ngpio) {
        DEBUG_LOG("Invalid parameters!");
        return;
    }

    DEBUG_LOG("Enter!");
    if (value) {
        virt_chip->cache_buf[offset] |= 0x01;
    } else {
        virt_chip->cache_buf[offset] &= ~0x01;
    }
    DEBUG_LOG("%s set gpio%d status as %s", virt_chip->label, offset, value ? "high" : "low");
}


int virt_gpioctrl_probe(struct platform_device *pdev)
{
    unsigned int ngpios = 0;
    const char *label = NULL;
    struct device *dev = &pdev->dev;
    struct device_node *np = dev->of_node;

    DEBUG_LOG("Enter!");
    if (!dev || !np) {
        DEBUG_LOG("Invalid device node!");
        return -EINVAL;
    }
    
    // 从设备树中获取gpio数量，label
    if (of_property_read_u32(np, "ngpios", &ngpios) < 0 ||
        of_property_read_string(np, "label", &label)) {
        DEBUG_LOG("Read data from dts failed!");
        return -EINVAL;
    }

    DEBUG_LOG("There are %d gpios in dts, name = %s!", ngpios, label);

    // 分配chip，为gpio子系统创建gpio_device提供关键信息；
    g_virt_gpiochip = devm_kzalloc(dev, sizeof(*g_virt_gpiochip), GFP_KERNEL);
    if (!g_virt_gpiochip) {
        DEBUG_LOG("Alloc gpio_chip memory failed!");
        return -ENOMEM;
    }

    g_virt_gpiochip->cache_buf = devm_kzalloc(dev, sizeof(u8) * ngpios, GFP_KERNEL);
    if (!g_virt_gpiochip->cache_buf) {
        DEBUG_LOG("Alloc cache memory failed!");
        return -ENOMEM;
    }

    // 设置gpio_chip，子系统会根据chip的参数来构建gpio_device，并根据ngpio的值来创建desc数组；
    //base info
    g_virt_gpiochip->chip.owner = THIS_MODULE;
    g_virt_gpiochip->label =  label;
    g_virt_gpiochip->chip.label = label;
	g_virt_gpiochip->chip.base = -1; // 不建议自定义base(可能会导致错误：GPIO integer space overlap)，建议赋值为-1，让系统自动分配，
    g_virt_gpiochip->ngpio = ngpios;
	g_virt_gpiochip->chip.ngpio = ngpios;
	g_virt_gpiochip->chip.can_sleep = false;
    g_virt_gpiochip->dev = dev;
	g_virt_gpiochip->chip.parent = dev;

    // func
    g_virt_gpiochip->chip.request = virt_gpioctrl_request;
    g_virt_gpiochip->chip.free = virt_gpioctrl_free;
    g_virt_gpiochip->chip.get_direction= virt_gpioctrl_get_direction;
    g_virt_gpiochip->chip.direction_output = virt_gpioctrl_direction_output;
    g_virt_gpiochip->chip.direction_input = virt_gpioctrl_direction_input;
    g_virt_gpiochip->chip.get = virt_gpioctrl_get;
    g_virt_gpiochip->chip.set = virt_gpioctrl_set;

    // 注册gpio_chip结构体
    if (gpiochip_add_data(&g_virt_gpiochip->chip, g_virt_gpiochip) != 0) {
        DEBUG_LOG("Register virtual gpioctrl chip failed!");
        return -EINVAL;
    }

    DEBUG_LOG("Probe end!");
    return 0;
}


int virt_gpioctrl_remove(struct platform_device *pdev)
{
    DEBUG_LOG("Enter!");

    // remove gpio_chip
    gpiochip_remove(&g_virt_gpiochip->chip);
    return 0;
}

static const struct of_device_id virt_gpioctrl_of_match[] = {
    { .compatible = "st,100ask_virt_gpioctrl" },
    {},
};

static struct platform_driver virt_gpioctrl_driver = {
    .probe		= virt_gpioctrl_probe,
    .remove		= virt_gpioctrl_remove,
    .driver		= {
        .name	= "virt_gpioctrl",
        .of_match_table = virt_gpioctrl_of_match,
    },
};

module_platform_driver(virt_gpioctrl_driver);
