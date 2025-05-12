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
#include <linux/platform_device.h>
#include <linux/ioport.h>
#include <linux/of.h>

#include "led_base_drv.h"
#include "stm32mp157_led_drv.h"

/**
 * STM32MP157开发板
 * 
 * LED资源:
 *  LED2: path = /dev/led_frame_drv0, pin = PA10
 *  LED3: path = /dev/led_frame_drv1, pin = PG8
 * 
 */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kline <kline_code@example,com>");
MODULE_DESCRIPTION("stm32mp157 chip led driver");

static volatile unsigned int *RCC_PLL4CR = NULL;
static volatile unsigned int *RCC_MP_AHB4ENSETR = NULL;
static volatile stm32mp157_gpio_regs *GPIO_REGS[MAX_GPIO_GROUP_NUM] = {0};

static int led_group_pin[32] = {0};
static int led_dev_cnt = 0;
static int current_led_num = -1;

static int check_led_num(int led_num)
{
    if (led_num != current_led_num) {
        DEBUG_LOG("FATAL ERROR: led_num changed, current=%d, input=%d", current_led_num, led_num);
        return -1;
    }
    return 0;
}

static void set_led_num(int led_num)
{
    current_led_num = led_num;
    // DEBUG_LOG("set current_led_num: %d", led_num);
}

static int get_led_location(int led_num, int *group, int *pin)
{
    int gpio_group = 0, gpio_pin = 0;
    if (check_led_num(led_num) || led_group_pin[led_num] < 0)
        return -1;

    if (led_num > led_dev_cnt) {
        DEBUG_LOG("Invalid led_num!");
        return -1;
    }
    gpio_group = (led_group_pin[led_num] >> 8) & 0xff;
    gpio_pin   = led_group_pin[led_num] & 0xff;
    if (gpio_group > 10 || gpio_pin > 15) {
        DEBUG_LOG("Invalid gpio group or pin!");
        return -1;
    }

    *group = gpio_group;
    *pin = gpio_pin;
    DEBUG_LOG("Get led%d gpio: group %d, pin %d", led_num, *group, *pin);
    return 0;
}

static int set_led_gpio_pin_mode(int led_num, unsigned char mode)
{
    int group = 0, pin = 0;
    if (get_led_location(led_num, &group, &pin)) {
        DEBUG_LOG("Get led gpio info failed!");
        return -1;
    }

    if (mode > 0b11) {
        DEBUG_LOG("Invalid mode: %d", mode);
        return -1;
    }

    if (((GPIO_REGS[group]->MODER >> (pin * 2)) & 0b11) == mode) {
        DEBUG_LOG("The mode has met expectation!");
        return 0;
    }

    // clear
    GPIO_REGS[group]->MODER &= ~(0b11 << pin*2);

    // set mode
    // low bit
    if (mode & (1 << 0)) {
        GPIO_REGS[group]->MODER |= (1 << pin*2);
    } else {
        GPIO_REGS[group]->MODER &= ~(1 << pin*2);
    }

    // high bit
    if (mode & (1 << 1)) {
        GPIO_REGS[group]->MODER |= (1 << (pin*2 + 1));
    } else {
        GPIO_REGS[group]->MODER &= ~(1 << (pin*2 + 1));
    }

    DEBUG_LOG("set gpio %c pin%d as mode %d", 'A' + group, pin, mode);
    return 0;
}

// 初始化函数
int stm32mp157_led_init(void)
{
    int group = 0;
    DEBUG_LOG("Enter!");
    // RCC register ioremap
    RCC_PLL4CR          = ioremap(RCC_REG_ADDR + 0x894, 4);
    RCC_MP_AHB4ENSETR   = ioremap(RCC_REG_ADDR + 0xA28, 4);

    for (group = 0; group < MAX_GPIO_GROUP_NUM; group++) {
        GPIO_REGS[group] = ioremap(GPIO_GROUP_REGS_BASE_ADDR(group), sizeof(stm32mp157_gpio_regs));
    }
    return 0;
}

// 配置函数，使硬件处于就绪状态
int stm32mp157_led_config(int led_num)
{
    int group = 0, pin = 0;
    set_led_num(led_num);
    if (get_led_location(led_num, &group, &pin)) {
        DEBUG_LOG("Get led gpio info failed!");
        return -1;
    }

    DEBUG_LOG("Enter with gpio group %c, pin %d!", 'A' + group, pin);
    // PLL4 enable
    *RCC_PLL4CR |= (1 << 0);
    while ((*RCC_PLL4CR & (1 << 1)) == 0);

    // 设置RCC对A7核生效，使能GPIO时钟
    *RCC_MP_AHB4ENSETR |= (1 << group);
    return 0;
}

// 控制函数，对led进行操控
int stm32mp157_led_ctrl(int led_num, char data)
{
    int group = 0, pin = 0;
    if (get_led_location(led_num, &group, &pin)) {
        DEBUG_LOG("Get led gpio info failed!");
        return -1;
    }

    // set output mode
    if (set_led_gpio_pin_mode(led_num, GPIO_OUTPUT_MODE)) {
        DEBUG_LOG("set GPIO output mode failed!");
        return -1;
    }

    if (data == LED_ON) {
        // led on, 给pin脚输出低电平
        GPIO_REGS[group]->BSRR = (1 << (16 + pin));
    } else {
        // led off，给pin脚输出高电平
        GPIO_REGS[group]->BSRR = (1 << pin);
    }
    DEBUG_LOG("Control gpio status success!");
    return 0;
}

// 检查led的状态
int stm32mp157_led_check(int led_num, char *status, int size)
{
    int group = 0, pin = 0;
    if (get_led_location(led_num, &group, &pin)) {
        DEBUG_LOG("Get led gpio info failed!");
        return -1;
    }

    if (!status) {
        DEBUG_LOG("Invalid parameter!");
        return -1;
    }

    if (GPIO_REGS[group]->IDR & (1 << pin)) {
        // 高电平代表led熄灭
        *status = LED_OFF;
    } else {
        // 低电平代表led点亮
        *status = LED_ON;
    }
    return 0;
}

int stm32mp157_led_release(int led_num)
{
    DEBUG_LOG("Enter!");
    if (check_led_num(led_num))
        return -1;

    set_led_num(-1);
    return 0;
}

// 释放所有的资源
void stm32mp157_led_destroy(void)
{
    int group = 0;

    // RCC
    if (RCC_PLL4CR && RCC_MP_AHB4ENSETR) {
        iounmap(RCC_PLL4CR);
        iounmap(RCC_MP_AHB4ENSETR);
        RCC_PLL4CR = NULL;
        RCC_MP_AHB4ENSETR = NULL;
    }

    for (group = 0; group < MAX_GPIO_GROUP_NUM; group++) {
        if (GPIO_REGS[group]) {
            iounmap(GPIO_REGS[group]);
            GPIO_REGS[group] = NULL;
        }
    }
}

struct led_operations g_led_oprts = {
    .init = stm32mp157_led_init,        // ioremap
    .config = stm32mp157_led_config,    // set led_num
    .ctrl = stm32mp157_led_ctrl,
    .check = stm32mp157_led_check,
    .release = stm32mp157_led_release,      // reset led_num
    .destroy = stm32mp157_led_destroy,  // iounmap
};

int stm32mp157_led_probe(struct platform_device *pdev)
{
    struct device_node	*pn = NULL;
    unsigned int u32_val = 0;
    if (!pdev) {
        DEBUG_LOG("Invalid platform device!");
        return -1;
    }

    pn = pdev->dev.of_node;
    if (!pn) {
        DEBUG_LOG("Cannot get device node from platform device!");
        return -1;
    }

    // 一个platform device对应一个device node
    if (of_property_read_u32(pn, "pin", &u32_val)) {
        DEBUG_LOG("Get value from device node failed!");
        return -1;
    }

    DEBUG_LOG("get device node: name=%s, path=%s, id=%d", pn->name, pn->full_name, pn->phandle);

    // register led operations
    snprintf(g_led_oprts.dev_name, MAX_LED_DEV_NAME_LEN, "%s", pn->name);
    if (led_operations_register(&g_led_oprts)) {
        DEBUG_LOG("register led operations failed!");
        return -1;
    }

    // create device
    if (led_device_create(led_dev_cnt)) {
        DEBUG_LOG("create devices%d failed!", led_dev_cnt);
        return -1;
    }
    led_group_pin[led_dev_cnt++] = u32_val;
    DEBUG_LOG("Probe device name: %s, led devices count: %d", g_led_oprts.dev_name, led_dev_cnt);
    return 0;
}

int stm32mp157_led_remove(struct platform_device *pdev)
{
    int i = 0, j = 0;
    struct device_node	*pn = NULL;
    unsigned int u32_val = 0;
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

    // 一个platform device对应一个device node
    if (of_property_read_u32(pn, "pin", &u32_val)) {
        DEBUG_LOG("Get value from device node failed!");
        return -1;
    }

    DEBUG_LOG("Remove device name: %s", pdev->name);
    for (i = 0, j = 0; i < led_dev_cnt; i++) {
        // 找到对应led，并销毁该led的设备节点
        if (led_group_pin[i] == u32_val) {
            if (led_device_destroy(i)) {
                DEBUG_LOG("destroy devices%d failed!", led_dev_cnt);
                return -1;
            }

            led_group_pin[i] = -1;
        }
        
        if (led_group_pin[i] == -1)
            j++;
    }

    if (j == led_dev_cnt) {
        // 表明所有灯都已经销毁，反注册led ops
        if (led_operations_unregister(&g_led_oprts)) {
            DEBUG_LOG("unregister led operations failed!");
            return -1;
        }
    }
    return 0;
}

static const struct of_device_id ask100_led_of_match[] = {
	{ .compatible = "100ask,led_dtb_drv", },
	{},
};

static struct platform_driver stm32mp157_led_driver = {
	.probe		= stm32mp157_led_probe,
	.remove		= stm32mp157_led_remove,
	.driver		= {
		.name	= "100ask_led",
        .of_match_table = ask100_led_of_match,
	},
};

module_platform_driver(stm32mp157_led_driver);

