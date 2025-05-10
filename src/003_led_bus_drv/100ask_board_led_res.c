#include <linux/platform_device.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include "led_base_drv.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kline <kline_code@example,com>");
MODULE_DESCRIPTION("Test for 100ask board led resource");

static void board_100ask_led_release(struct device *dev)
{
    // 这里可以释放设备占用的私有资源（如果有）
    DEBUG_LOG("100ask board released");
}

static struct resource board_100ask_led_res[] = {
    {
        .start = GROUP_PIN(0, 10),
        .flags = IORESOURCE_IRQ,
    },
    {
        .start = GROUP_PIN(6, 8),
        .flags = IORESOURCE_IRQ,
    },
};

static struct platform_device board_100ask_led_pdev = {
    .name = "100ask_led",
    .id = 0,
    .num_resources = ARRAY_SIZE(board_100ask_led_res),
    .resource = board_100ask_led_res,
    .dev = {
        .release = board_100ask_led_release, // 关键修复！
    },
};


static int __init board_100ask_led_device_init(void)
{
    DEBUG_LOG("Enter!");
    if (platform_device_register(&board_100ask_led_pdev)) {
        DEBUG_LOG("register platform device failed!");
        return -1;
    }
    return 0;
}

static void __exit board_100ask_led_device_exit(void)
{
    DEBUG_LOG("Enter!");
    platform_device_unregister(&board_100ask_led_pdev);
    DEBUG_LOG("End!");
}

module_init(board_100ask_led_device_init);
module_exit(board_100ask_led_device_exit);
