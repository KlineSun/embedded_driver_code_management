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

#include "button_base_drv.h"
#include "stm32mp157_button_drv.h"

/**
 * STM32MP157开发板
 * 
 * button资源:
 *  KEY1: path = /dev/button_frame_drv0, pin = PA10
 *  KEY2: path = /dev/button_frame_drv1, pin = PG8
 * 
 */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kline <kline_code@example,com>");
MODULE_DESCRIPTION("stm32mp157 chip button driver");

static volatile unsigned int *RCC_PLL4CR = NULL;
static volatile unsigned int *RCC_MP_AHB4ENSETR = NULL;
static volatile stm32mp157_gpio_regs *GPIO_REGS[MAX_GPIO_GROUP_NUM] = {0};

static int btn_group_pin[32] = {0};
static int btn_dev_cnt = 0;
static int current_btn_num = -1;

static int check_btn_num(int btn_num)
{
    if (btn_num != current_btn_num) {
        DEBUG_LOG("FATAL ERROR: btn_num changed, current=%d, input=%d", current_btn_num, btn_num);
        return -1;
    }
    return 0;
}

static void set_btn_num(int btn_num)
{
    current_btn_num = btn_num;
    // DEBUG_LOG("set current_btn_num: %d", btn_num);
}

static int get_btn_location(int btn_num, int *group, int *pin)
{
    int gpio_group = 0, gpio_pin = 0;
    if (check_btn_num(btn_num) || btn_group_pin[btn_num] < 0)
        return -1;

    if (btn_num > btn_dev_cnt) {
        DEBUG_LOG("Invalid btn_num!");
        return -1;
    }
    gpio_group = (btn_group_pin[btn_num] >> 8) & 0xff;
    gpio_pin   = btn_group_pin[btn_num] & 0xff;
    if (gpio_group > 10 || gpio_pin > 15) {
        DEBUG_LOG("Invalid gpio group or pin!");
        return -1;
    }

    *group = gpio_group;
    *pin = gpio_pin;
    DEBUG_LOG("Get button%d gpio: group %d, pin %d", btn_num, *group, *pin);
    return 0;
}

/*static int set_button_gpio_pin_mode(int btn_num, unsigned char mode)
{
    int group = 0, pin = 0;
    if (get_btn_location(btn_num, &group, &pin)) {
        DEBUG_LOG("Get button gpio info failed!");
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
}*/

// 初始化函数
int stm32mp157_button_init(void)
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
int stm32mp157_button_config(int btn_num)
{
    int group = 0, pin = 0;
    set_btn_num(btn_num);
    if (get_btn_location(btn_num, &group, &pin)) {
        DEBUG_LOG("Get button gpio info failed!");
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

int stm32mp157_button_ctrl(int btn_num, char data)
{
    int group = 0, pin = 0;
    if (get_btn_location(btn_num, &group, &pin)) {
        DEBUG_LOG("Get button gpio info failed!");
        return -1;
    }

    // 控制函数暂未定义
    // set output mode
    // if (set_button_gpio_pin_mode(btn_num, GPIO_OUTPUT_MODE)) {
    //     DEBUG_LOG("set GPIO output mode failed!");
    //     return -1;
    // }

    // if (data == KEY_DOWN) {
    //     // button on, 给pin脚输出低电平
    //     GPIO_REGS[group]->BSRR = (1 << (16 + pin));
    // } else {
    //     // button off，给pin脚输出高电平
    //     GPIO_REGS[group]->BSRR = (1 << pin);
    // }
    DEBUG_LOG("Control gpio status success!");
    return 0;
}

int stm32mp157_button_check(int btn_num, char *status, int size)
{
    int group = 0, pin = 0;
    if (get_btn_location(btn_num, &group, &pin)) {
        DEBUG_LOG("Get button gpio info failed!");
        return -1;
    }

    if (!status) {
        DEBUG_LOG("Invalid parameter!");
        return -1;
    }

    if (GPIO_REGS[group]->IDR & (1 << pin)) {
        // 高电平代表button未被按下
        *status = KEY_UP;
    } else {
        // 低电平代表button被按下
        *status = KEY_DOWN;
    }
    return 0;
}

int stm32mp157_button_release(int btn_num)
{
    DEBUG_LOG("Enter!");
    if (check_btn_num(btn_num))
        return -1;

    set_btn_num(-1);
    return 0;
}

// 释放所有的资源
void stm32mp157_button_destroy(void)
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

struct button_operations g_button_oprts = {
    .init = stm32mp157_button_init,        // ioremap
    .config = stm32mp157_button_config,    // set btn_num
    .ctrl = stm32mp157_button_ctrl,
    .check = stm32mp157_button_check,
    .release = stm32mp157_button_release,      // reset btn_num
    .destroy = stm32mp157_button_destroy,  // iounmap
};

int stm32mp157_button_probe(struct platform_device *pdev)
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

    // register button operations
    snprintf(g_button_oprts.dev_name, MAX_DEV_NAME_LEN, "%s", pn->name);
    if (button_operations_register(&g_button_oprts)) {
        DEBUG_LOG("register button operations failed!");
        return -1;
    }

    // create device
    if (button_device_create(btn_dev_cnt)) {
        DEBUG_LOG("create devices%d failed!", btn_dev_cnt);
        return -1;
    }
    btn_group_pin[btn_dev_cnt++] = u32_val;
    DEBUG_LOG("Probe device name: %s, button devices count: %d", g_button_oprts.dev_name, btn_dev_cnt);
    return 0;
}

int stm32mp157_button_remove(struct platform_device *pdev)
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
    for (i = 0, j = 0; i < btn_dev_cnt; i++) {
        // 找到对应button，并销毁该button的设备节点
        if (btn_group_pin[i] == u32_val) {
            if (button_device_destroy(i)) {
                DEBUG_LOG("destroy devices%d failed!", btn_dev_cnt);
                return -1;
            }

            btn_group_pin[i] = -1;
        }
        
        if (btn_group_pin[i] == -1)
            j++;
    }

    if (j == btn_dev_cnt) {
        DEBUG_LOG("All pdev had destroy!");
        // 表明所有灯都已经销毁，反注册button ops
        if (button_operations_unregister(&g_button_oprts)) {
            DEBUG_LOG("unregister button operations failed!");
            return -1;
        }
    }
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

