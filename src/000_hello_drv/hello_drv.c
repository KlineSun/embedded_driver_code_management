#include <linux/module.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/tty.h>
#include <linux/kmod.h>
#include <linux/gfp.h>


// 8、完善证书(GPL)、作者、驱动简介等信息
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kline <kline_code@example,com>");
MODULE_DESCRIPTION("Test for hello driver");

#define MAX_DRV_CAHCE_LEN (1024)
#define MIN(x, y) x < y ? x : y
#define DEBUG_LOG(fmt, ...) \
        printk(KERN_INFO "[%s line%d] %s(): "fmt"\n", \
            KBUILD_BASENAME, __LINE__, __FUNCTION__, ##__VA_ARGS__)

// 1、 确定主设备号，也可以让内核分配(注册时传入0);
static int major = 0;
static char g_cache_buf[MAX_DRV_CAHCE_LEN] = {0};
static char g_cp_buf[MAX_DRV_CAHCE_LEN] = {0};
static struct class *hello_drv_class = NULL;

// 3、 实现对应的 open/read/write 等函数，填入 file_operations 结构体；
static int hello_drv_open(struct inode *inode, struct file *file)
{
    DEBUG_LOG("Enter!");
    return 0;
}

static ssize_t hello_drv_read(struct file *file, char __user *buf, size_t size, loff_t *offset)
{
    if (*offset < 0 || *offset > MAX_DRV_CAHCE_LEN) {
        DEBUG_LOG("Invalid parameter!");
        return -1;
    }

    DEBUG_LOG("Enter!");
    copy_to_user(buf, g_cache_buf + *offset, MIN(size, MAX_DRV_CAHCE_LEN - *offset));
    return 1;
}

static ssize_t hello_drv_write(struct file *file, const char __user *buf, size_t size, loff_t *offset)
{
    if (*offset < 0) {
        DEBUG_LOG("Invalid parameter!");
        return -1;
    }

    DEBUG_LOG("Enter!");
    memset(g_cp_buf, 0, sizeof(g_cp_buf));
    copy_from_user(g_cp_buf, buf, MIN(size, MAX_DRV_CAHCE_LEN));
    DEBUG_LOG("Get data from user: %s", g_cp_buf);
    memcpy(g_cache_buf + *offset, g_cp_buf, MIN(size, MAX_DRV_CAHCE_LEN - *offset));
    DEBUG_LOG("Current cache data: %s", g_cache_buf);
    return 1;
}

static long hello_drv_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    DEBUG_LOG("Enter!");
    return 1;
}

static int hello_drv_close(struct inode *inode, struct file *file)
{
    DEBUG_LOG("Enter!");
    return 0;
}

// 2、 定义驱动自己的file_operations结构体；
static struct file_operations hello_drv_fop = {
    .owner =            THIS_MODULE,
    .open =             hello_drv_open,
    .read =             hello_drv_read,
    .write =            hello_drv_write,
    .unlocked_ioctl =   hello_drv_ioctl,
    .release =          hello_drv_close,
};


// 7、 其他完善：提供设备信息，自动创建设备节点：class_create, device_create
static int __init hello_drv_init(void)
{
    int err;
    struct device *hello_drv_device;

    DEBUG_LOG("Enter!");
// 4、 把 file_operations 结构体告诉内核：register_chrdev
    // register
    major = register_chrdev(0, "hello_drv", &hello_drv_fop);
    if (major < 0) {
        DEBUG_LOG("Init hello drv failed!");
        return -1;
    }
    DEBUG_LOG("get device major num: %d", major);

// 5、 其他完善：提供设备信息，自动创建设备节点：class_create,  device_create
    // class create
	hello_drv_class = class_create(THIS_MODULE, "hello_drv");
    err = PTR_ERR(hello_drv_class);
	if (IS_ERR(hello_drv_class)) {
        DEBUG_LOG("Create hello drv class failed!");
        return -1;
    }

    // device create: /dev/hello_drv
    hello_drv_device = device_create(hello_drv_class, NULL, MKDEV(major, 0), NULL, "hello_drv");

    DEBUG_LOG("Init hello drv success!");
    return 0;
}

static void __exit hello_drv_exit(void)
{
    DEBUG_LOG("Enter!");
    device_destroy(hello_drv_class, MKDEV(major, 0));

    class_destroy(hello_drv_class);
	/*
	 * Unregister the character device interface to the driver.
	 */
	unregister_chrdev(major, "hello_drv");
}

// 6、指明当前驱动的入口函数，安装驱动程序(insmod)时，就会去调用这个入口函数；
module_init(hello_drv_init);
// 7、指明当前驱动的入口函数，卸载驱动程序(rmmod)时，调用出口函数，进而执行unregister_chrdev;
module_exit(hello_drv_exit);

