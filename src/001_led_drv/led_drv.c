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


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kline <kline_code@example,com>");
MODULE_DESCRIPTION("Test for led simple driver");

#define MAX_DRV_CAHCE_LEN (1024)
#define MIN(x, y) x < y ? x : y
#define DEBUG_LOG(fmt, ...) \
        printk(KERN_INFO "[%s line%d] %s(): "fmt"\n", \
            KBUILD_BASENAME, __LINE__, __FUNCTION__, ##__VA_ARGS__)


/* register begin */
static volatile unsigned int *RCC_PLL4CR;
static volatile unsigned int *RCC_MP_AHB4ENSETR;
static volatile unsigned int *GPIOA_MODER;
static volatile unsigned int *GPIOA_IDR;
static volatile unsigned int *GPIOA_ODR;
static volatile unsigned int *GPIOA_BSRR;
/* register begin */

static int major = 0;
static struct class *led_drv_class = NULL;

static int enable_a7_gpio_clk(char gpio_num)
{
    int bit_offset = 0;
    if (gpio_num >= 'A' && gpio_num <= 'Z') {
        bit_offset = gpio_num - 'A';
    } else if (gpio_num >= 'a' && gpio_num <= 'z') {
        bit_offset = gpio_num - 'a';
    } else {
        DEBUG_LOG("Unsupport gpio number!");
        return -1;
    }

    DEBUG_LOG("gpio num bit offset: %d", bit_offset);
    *RCC_MP_AHB4ENSETR |= (1 << bit_offset);
    return 0;
}

static int set_gpioa_pin_mode(int pin_num, unsigned char mode)
{
    char old_val = 0;
    if (pin_num > 15 || mode > 0b11) {
        DEBUG_LOG("Invalid parameter!");
        return -1;
    }

    old_val = (*GPIOA_MODER >> pin_num * 2) & 0x03;
    DEBUG_LOG("old mode: %d", old_val);
    if (old_val == mode) {
        DEBUG_LOG("The mode has met expectation!");
        return 0;
    }

    // clear
    *GPIOA_MODER &= ~(0b11 << pin_num*2);

    // set mode
    // low bit
    if (mode & (1 << 0)) {
        *GPIOA_MODER |= (1 << pin_num*2);
    } else {
        *GPIOA_MODER &= ~(1 << pin_num*2);
    }

    // high bit
    if (mode & (1 << 1)) {
        *GPIOA_MODER |= (1 << (pin_num*2 + 1));
    } else {
        *GPIOA_MODER &= ~(1 << (pin_num*2 + 1));
    }

    DEBUG_LOG("set gpio pin%d as mode %d", pin_num, mode);
    return 0;
}

static int led_drv_open(struct inode *inode, struct file *file)
{
    unsigned int open_flag = file->f_flags;
    DEBUG_LOG("Enter!");

    // PLL4 enable
    *RCC_PLL4CR |= (1 << 0);
    while ((*RCC_PLL4CR & (1 << 1)) == 0);

    // 设置RCC对A7核生效，使能GPIOA时钟
    if (enable_a7_gpio_clk('A')) {
        DEBUG_LOG("enable gpio clk failed!");
        return -1;
    }

    if ((open_flag & O_ACCMODE) == O_WRONLY) {
        DEBUG_LOG("open by write only!");
    } else if ((open_flag & O_ACCMODE) == O_RDONLY) {
        DEBUG_LOG("open by read only!");
    }

    return 0;
}

static ssize_t led_drv_read(struct file *file, char __user *buf, size_t size, loff_t *offset)
{
    char bit_val = 1;
    if (*offset < 0 || *offset > MAX_DRV_CAHCE_LEN) {
        DEBUG_LOG("Invalid parameter!");
        return -1;
    }

    DEBUG_LOG("Enter!");
    if (set_gpioa_pin_mode(10, 0)) {
        DEBUG_LOG("Set input mode failed!");
        return -1;
    }

    if (*GPIOA_IDR & (1 << 10)) {
        // 高电平代表led熄灭
        bit_val = 0;
    }

    DEBUG_LOG("led current status: %s", bit_val ? "ON" : "OFF");
    copy_to_user(buf, &bit_val, 1);
    return 1;
}

static ssize_t led_drv_write(struct file *file, const char __user *buf, size_t size, loff_t *offset)
{
    char bit_val = 0;
    if (*offset < 0) {
        DEBUG_LOG("Invalid parameter!");
        return -1;
    }

    DEBUG_LOG("Enter!");
    if (set_gpioa_pin_mode(10, 1)) {
        DEBUG_LOG("Set output mode failed!");
        return -1;
    }

    copy_from_user(&bit_val, buf, 1);

    if (bit_val) {
        // led on，PA10输出低电平
        *GPIOA_BSRR = (1 << 26);
    } else {
        // led off，PA10输出高电平
        *GPIOA_BSRR = (1 << 10);
    }
    DEBUG_LOG("set PA10 success!");
    return 1;
}

static long led_drv_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    DEBUG_LOG("Enter!");
    return 1;
}

static int led_drv_close(struct inode *inode, struct file *file)
{
    DEBUG_LOG("Enter!");
    return 0;
}

static struct file_operations led_drv_fop = {
    .owner =            THIS_MODULE,
    .open =             led_drv_open,
    .read =             led_drv_read,
    .write =            led_drv_write,
    .unlocked_ioctl =   led_drv_ioctl,
    .release =          led_drv_close,
};


static int __init led_drv_init(void)
{
    struct device *led_drv_device;

    DEBUG_LOG("Enter!");
    // register
    major = register_chrdev(0, "led_simple_drv", &led_drv_fop);
    if (major < 0) {
        DEBUG_LOG("Init led drv failed!");
        return -1;
    }
    DEBUG_LOG("get device major num: %d", major);
    // class create
	led_drv_class = class_create(THIS_MODULE, "led_simple_drv");
	if (IS_ERR(led_drv_class)) {
        DEBUG_LOG("Create led drv class failed!");
        unregister_chrdev(major, "led_simple_drv");
        return -1;
    }

    led_drv_device = device_create(led_drv_class, NULL, MKDEV(major, 0), NULL, "led_simple_drv");
    if (IS_ERR(led_drv_device)) {
		DEBUG_LOG("Create led drv class failed!");
        class_destroy(led_drv_class);
        unregister_chrdev(major, "led_simple_drv");
        return -1;
	}

    /* register address ioremap */
    // RCC_PLL4CR: 0x50000000 + 0x894
    RCC_PLL4CR = ioremap(0x50000000 + 0x894, 4);

    // RCC_MP_AHB4ENSETR: 0x50000000 + 0xA28
    RCC_MP_AHB4ENSETR = ioremap(0x50000000 + 0xA28, 4);

    // GPIOA_MODER: 0x50002000 + 0x00
    GPIOA_MODER = ioremap(0x50002000 + 0x00, 4);

    // GPIOA_IDR: 0x50002000 + 0x10
    GPIOA_IDR = ioremap(0x50002000 + 0x10, 4);

    // GPIOA_ODR: 0x50002000 + 0x14
    GPIOA_ODR = ioremap(0x50002000 + 0x14, 4);

    // GPIOA_BSRR: 0x50002000 + 0x18
    GPIOA_BSRR = ioremap(0x50002000 + 0x18, 4);

    DEBUG_LOG("Init led drv success!");
    return 0;
}

static void __exit led_drv_exit(void)
{
    DEBUG_LOG("Enter!");

    /* register address iounmap */
    iounmap(RCC_PLL4CR);
    iounmap(RCC_MP_AHB4ENSETR);
    iounmap(GPIOA_MODER);
    iounmap(GPIOA_IDR);
    iounmap(GPIOA_ODR);
    iounmap(GPIOA_BSRR);

    device_destroy(led_drv_class, MKDEV(major, 0));

    class_destroy(led_drv_class);
	/*
	 * Unregister the character device interface to the driver.
	 */
	unregister_chrdev(major, "led_simple_drv");
}

module_init(led_drv_init);
module_exit(led_drv_exit);
