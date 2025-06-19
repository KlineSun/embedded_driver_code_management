#include <linux/module.h>
#include <linux/fs.h>
#include <linux/ioport.h>
#include <linux/uaccess.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/init.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kline <kline_code@example.com>");
MODULE_DESCRIPTION("Test for ap3216c i2c client");

static struct i2c_adapter *g_i2c_adap = NULL;
static struct i2c_client *g_i2c_clnt = NULL;

static struct i2c_board_info ap3216c_i2c_board_info[] __initdata = {
    {
        I2C_BOARD_INFO("ap3216c", 0x1e),
    },
};

static int __init ap3216c_i2c_client_init(void)
{
    printk("i2c client init!\n");
    g_i2c_adap = i2c_get_adapter(0);
    g_i2c_clnt = i2c_new_device(g_i2c_adap, ap3216c_i2c_board_info);
    if (!g_i2c_clnt || !g_i2c_adap) {
        printk("i2c_new_device failed\n");
        return -1;
    }

    printk("Init success!\n");
    return 0;
}

static void __exit ap3216c_i2c_client_exit(void)
{
    printk("i2c client exit!\n");
    if (g_i2c_clnt)
        i2c_unregister_device(g_i2c_clnt);
}

module_init(ap3216c_i2c_client_init);
module_exit(ap3216c_i2c_client_exit);
