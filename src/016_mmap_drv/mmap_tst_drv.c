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
#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/interrupt.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/wait.h>
#include <linux/jiffies.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include "mmap_tst_drv.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kline <kline_code@example,com>");
MODULE_DESCRIPTION("Test for button bus driver");

#define MMAP_SHARE_BUF_SIZE (4096)

static struct class *mmap_drv_class = NULL;
static  int major = 0;
static char *g_shared_buffer = NULL;

static int local_tst_drv_open(struct inode *inode, struct file *file)
{
    int minor = iminor(inode);

    DEBUG_LOG("Enter with minor: %d", minor);
    DEBUG_LOG("g_shared_buffer = %s", g_shared_buffer);
    return 0;
}

static ssize_t local_tst_drv_write(struct file *file, const char __user *buf, size_t size, loff_t *offset)
{
    char bit_val = 0;
    struct inode *inode = file_inode(file);
    int minor = iminor(inode);

    DEBUG_LOG("Enter with minor: %d", minor);
    copy_from_user(&bit_val, buf, 1);

    return 1;
}

static ssize_t local_tst_drv_read(struct file *file, char __user *buf, size_t size, loff_t *offset)
{
    struct inode *inode = file_inode(file);
    int minor = iminor(inode);
    int val = 0;

    DEBUG_LOG("Enter with minor: %d", minor);
    copy_to_user(buf, &val, 1);
    return 1;
}

static long local_tst_drv_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct inode *inode = file_inode(file);
    int minor = iminor(inode);

    DEBUG_LOG("Enter with minor: %d", minor);
    return 1;
}

static int local_tst_drv_mmap(struct file *filp, struct vm_area_struct *vma)
{
    unsigned long offset = (vma->vm_pgoff) << PAGE_SHIFT; // 将页号还原成字节数
    unsigned long size   = vma->vm_end - vma->vm_start;
    unsigned long paddr = 0;
    struct inode *inode = file_inode(filp);
    int minor = iminor(inode);

    if (offset + size > MMAP_SHARE_BUF_SIZE) {
        DEBUG_LOG("request memory size out of realize size: size=%ld, offset=%ld", size, offset);
        return -1;
    }

    DEBUG_LOG("Enter with minor: %d", minor);
    vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

    paddr = virt_to_phys(g_shared_buffer);

    return remap_pfn_range(vma,
                    vma->vm_start,
                    paddr >> PAGE_SHIFT, // 获得物理地址的页号
                    size,
                    vma->vm_page_prot);
}

static int local_tst_drv_close(struct inode *inode, struct file *file)
{
    int minor = iminor(inode);

    DEBUG_LOG("Enter with minor: %d", minor);
    DEBUG_LOG("g_shared_buffer = %s", g_shared_buffer);
    return 0;
}

static struct file_operations local_tst_drv_fop = {
    .owner =            THIS_MODULE,
    .open =             local_tst_drv_open,
    .read =             local_tst_drv_read,
    .write =            local_tst_drv_write,
    .unlocked_ioctl =   local_tst_drv_ioctl,
    .mmap =             local_tst_drv_mmap,
    .release =          local_tst_drv_close,
};


static int __init led_drv_init(void)
{
    struct device *mmap_drv_device;

    DEBUG_LOG("Enter!");
    // register
    major = register_chrdev(0, "mmap_drv", &local_tst_drv_fop);
    if (major < 0) {
        DEBUG_LOG("Init mmap drv failed!");
        return -1;
    }
    DEBUG_LOG("get device major num: %d", major);
    // class create
	mmap_drv_class = class_create(THIS_MODULE, "mmap_drv");
	if (IS_ERR(mmap_drv_class)) {
        DEBUG_LOG("Create mmap drv class failed!");
        goto chredv_free;
    }

    mmap_drv_device = device_create(mmap_drv_class, NULL, MKDEV(major, 0), NULL, "mmap_drv");
    if (IS_ERR(mmap_drv_device)) {
		DEBUG_LOG("Create mmap drv class failed!");
        goto class_free;
	}

    g_shared_buffer = kzalloc(MMAP_SHARE_BUF_SIZE, GFP_KERNEL);
    if (!g_shared_buffer) {
        DEBUG_LOG("kernel alloc memory failed!");
        goto class_free;
    }
    return 0;

class_free:
    if (!mmap_drv_class)
        class_destroy(mmap_drv_class);

chredv_free:
    if (major > 0)
        unregister_chrdev(major, "mmap_drv");
    major = 0;
    return -1;
}

static void __exit led_drv_exit(void)
{
    DEBUG_LOG("Enter!");

    if (!g_shared_buffer)
        kfree(g_shared_buffer);

    device_destroy(mmap_drv_class, MKDEV(major, 0));

    class_destroy(mmap_drv_class);
	/*
	 * Unregister the character device interface to the driver.
	 */
	unregister_chrdev(major, "mmap_drv");
}

module_init(led_drv_init);
module_exit(led_drv_exit);

