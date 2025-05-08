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
#include "led_drv.h"
#include "stm32mp157_led_config.h"

/**
 * STM32MP157开发板
 * 
 * LED资源:
 *  LED2: path = /dev/led_frame_drv0, pin = PA10
 *  LED3: path = /dev/led_frame_drv1, pin = PG8
 * 
 */

static volatile unsigned int *RCC_PLL4CR = NULL;
static volatile unsigned int *RCC_MP_AHB4ENSETR = NULL;
static volatile stm32mp157_gpio_regs *GPIO_REGS = NULL;
static int PIN_NUM = -1;
static int GPIO_GROUP_NUM = -1;

static volatile stm32mp157_gpio_regs *g_gpio_A_regs = NULL;
static volatile stm32mp157_gpio_regs *g_gpio_G_regs = NULL;


static int enable_stm32_a7_gpio_clk(char gpio_num)
{
    int set_bit_num = 0;
    if (gpio_num >= 'A' && gpio_num <= 'K') {
        set_bit_num = gpio_num - 'A';
    } else if (gpio_num >= 'a' && gpio_num <= 'k') {
        set_bit_num = gpio_num - 'a';
    } else {
        DEBUG_LOG("Unsupport gpio number!");
        return -1;
    }

    DEBUG_LOG("gpio num %c bit offset: %d", gpio_num, set_bit_num);
    *RCC_MP_AHB4ENSETR |= (1 << set_bit_num);
    return 0;
}

static unsigned char get_led_gpio_pin_mode(void)
{
    char mode = (GPIO_REGS->MODER >> (PIN_NUM * 2)) & 0b11;
    DEBUG_LOG("led gpio current mode: %d", mode);
    return mode;
}

static int set_led_gpio_pin_mode(unsigned char mode)
{
    if (mode > 0b11) {
        DEBUG_LOG("Invalid mode: %d", mode);
        return -1;
    }

    if (get_led_gpio_pin_mode() == mode) {
        DEBUG_LOG("The mode has met expectation!");
        return 0;
    }

    // clear
    GPIO_REGS->MODER &= ~(0b11 << PIN_NUM*2);

    // set mode
    // low bit
    if (mode & (1 << 0)) {
        GPIO_REGS->MODER |= (1 << PIN_NUM*2);
    } else {
        GPIO_REGS->MODER &= ~(1 << PIN_NUM*2);
    }

    // high bit
    if (mode & (1 << 1)) {
        GPIO_REGS->MODER |= (1 << (PIN_NUM*2 + 1));
    } else {
        GPIO_REGS->MODER &= ~(1 << (PIN_NUM*2 + 1));
    }

    DEBUG_LOG("set gpio %c pin%d as mode %d", 'A' + GPIO_GROUP_NUM, PIN_NUM, mode);
    return 0;
}

// 初始化函数
int stm32mp157_led_init(void)
{
    DEBUG_LOG("Enter!");
    // RCC register ioremap
    RCC_PLL4CR          = ioremap(RCC_REG_ADDR + 0x894, 4);
    RCC_MP_AHB4ENSETR   = ioremap(RCC_REG_ADDR + 0xA28, 4);

    // GPIOA register ioremap
    g_gpio_A_regs = ioremap(GPIO_A_REG_ADDR, sizeof(stm32mp157_gpio_regs));

    // GPIOG register ioremap
    g_gpio_G_regs = ioremap(GPIO_G_REG_ADDR, sizeof(stm32mp157_gpio_regs));

    // global arguments reset
    GPIO_REGS = NULL;
    PIN_NUM = -1;
    GPIO_GROUP_NUM = -1;
    return 0;
}

// 配置函数，使硬件处于就绪状态
int stm32mp157_led_config(int led_num)
{
    int ret = 0;
    // DEBUG_LOG("Enter!");

    // PLL4 enable
    *RCC_PLL4CR |= (1 << 0);
    while ((*RCC_PLL4CR & (1 << 1)) == 0);

    // 设置RCC对A7核生效，使能GPIO时钟
    if (led_num == 0) { // led0: GPIOA 10
        
        ret = enable_stm32_a7_gpio_clk('A');
        GPIO_REGS = g_gpio_A_regs;
        PIN_NUM = 10;
        GPIO_GROUP_NUM = 0;
    } else if (led_num == 1) { // led1: GPIOG 8
        ret = enable_stm32_a7_gpio_clk('G');
        GPIO_REGS = g_gpio_G_regs;
        PIN_NUM = 8;
        GPIO_GROUP_NUM = 'G' - 'A';
    }
    if (ret) {
        DEBUG_LOG("enable gpio clk failed!");
        GPIO_REGS = NULL;
        PIN_NUM = -1;
        GPIO_GROUP_NUM = -1;
        return -1;
    }

    DEBUG_LOG("Enable GPIO %c clk, pin%d", 'A' + GPIO_GROUP_NUM, PIN_NUM);
    return 0;
}

// 控制函数，对led进行操控
int stm32mp157_led_ctrl(int led_num, char data)
{
    if (!GPIO_REGS || PIN_NUM < 0 || PIN_NUM > 15) {
        DEBUG_LOG("invalid gpio register address or pin number!");
        return -1;
    }

    // set output mode
    if (set_led_gpio_pin_mode(GPIO_OUTPUT_MODE)) {
        DEBUG_LOG("set GPIO output mode failed!");
        return -1;
    }

    if (data) {
        // led on, 给pin脚输出低电平
        GPIO_REGS->BSRR = (1 << (16 + PIN_NUM));
    } else {
        // led off，给pin脚输出高电平
        GPIO_REGS->BSRR = (1 << PIN_NUM);
    }
    DEBUG_LOG("Control gpio status success!");
    return 0;
}

// 检查led的状态
int stm32mp157_led_check(int led_num, char *status, int size)
{
    if (!status) {
        DEBUG_LOG("Invalid parameter!");
        return -1;
    }

    if (!GPIO_REGS || PIN_NUM < 0 || PIN_NUM > 15) {
        DEBUG_LOG("invalid gpio register address or pin number!");
        return -1;
    }

    if (GPIO_REGS->IDR & (1 << PIN_NUM)) {
        // 高电平代表led熄灭
        *status = LED_OFF;
    } else {
        // 低电平代表led点亮
        *status = LED_ON;
    }
    return 0;
}

// 释放所有的资源
void stm32mp157_led_destroy(void)
{
    // DEBUG_LOG("Enter!");

    // RCC register
    iounmap(RCC_PLL4CR);
    iounmap(RCC_MP_AHB4ENSETR);
    // GPIOA
    iounmap(g_gpio_A_regs);
    // GPIOG
    iounmap(g_gpio_G_regs);

    // global arguments reset
    GPIO_REGS = NULL;
    PIN_NUM = -1;
    GPIO_GROUP_NUM = -1;
}

struct led_operations g_led_oprts = {
    .num  = 2,
    .init = stm32mp157_led_init,
    .config = stm32mp157_led_config,
    .ctrl = stm32mp157_led_ctrl,
    .check = stm32mp157_led_check,
    .destroy = stm32mp157_led_destroy,
};

struct led_operations *get_led_oprts()
{
    return &g_led_oprts;
}

