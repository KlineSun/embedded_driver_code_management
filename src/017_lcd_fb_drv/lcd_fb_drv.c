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
#include <linux/platform_device.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <asm/mach/map.h>
#include <linux/fb.h>
#include <linux/dma-mapping.h>
#include "lcd_fb_drv.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kline <kline_code@example,com>");
MODULE_DESCRIPTION("Test for button bus driver");

#define MAX_LCD_NAME_LEN (16)

struct stm32_fbinfo {
    char name[MAX_LCD_NAME_LEN];
    struct device *dev;
};

static struct fb_info *g_fbinfo = NULL;
//static struct stm32_fbinfo stm_fbinfo;

// static int local_tst_drv_open(struct inode *inode, struct file *file)
// {
//     int minor = iminor(inode);

//     DEBUG_LOG("Enter with minor: %d", minor);
//     return 0;
// }

// static ssize_t local_tst_drv_write(struct file *file, const char __user *buf, size_t size, loff_t *offset)
// {
//     char bit_val = 0;
//     struct inode *inode = file_inode(file);
//     int minor = iminor(inode);

//     DEBUG_LOG("Enter with minor: %d", minor);
//     copy_from_user(&bit_val, buf, 1);

//     return 1;
// }

// static ssize_t local_tst_drv_read(struct file *file, char __user *buf, size_t size, loff_t *offset)
// {
//     struct inode *inode = file_inode(file);
//     int minor = iminor(inode);
//     int val = 0;

//     DEBUG_LOG("Enter with minor: %d", minor);
//     copy_to_user(buf, &val, 1);
//     return 1;
// }

// static long local_tst_drv_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
// {
//     struct inode *inode = file_inode(file);
//     int minor = iminor(inode);

//     DEBUG_LOG("Enter with minor: %d", minor);
//     return 1;
// }

// static int local_tst_drv_mmap(struct file *filp, struct vm_area_struct *vma)
// {
//     unsigned long offset = (vma->vm_pgoff) << PAGE_SHIFT; // 将页号还原成字节数
//     unsigned long size   = vma->vm_end - vma->vm_start;
//     unsigned long paddr = 0;
//     struct inode *inode = file_inode(filp);
//     int minor = iminor(inode);

//     DEBUG_LOG("Enter with minor: %d", minor);
//     vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

//     return remap_pfn_range(vma,
//                     vma->vm_start,
//                     paddr >> PAGE_SHIFT, // 获得物理地址的页号
//                     size,
//                     vma->vm_page_prot);
// }

// static int local_tst_drv_close(struct inode *inode, struct file *file)
// {
//     int minor = iminor(inode);

//     DEBUG_LOG("Enter with minor: %d", minor);
//     return 0;
// }

static struct fb_ops stm32_fb_ops = {
    .owner		    = THIS_MODULE,
    // .fb_open	    = local_tst_drv_open,
    // .fb_read        = local_tst_drv_read,
    // .fb_write	    = local_tst_drv_write,
    // .fb_ioctl 	    = local_tst_drv_ioctl,
    // .fb_release	    = local_tst_drv_close,
    // 非常规file接口
    // .fb_pan_display	= arcfb_pan_display,
    .fb_fillrect	= cfb_fillrect,
    .fb_copyarea	= cfb_copyarea,
    .fb_imageblit	= cfb_imageblit,
};

static int stm32mp157_map_video_memory(struct fb_info *info)
{
    dma_addr_t map_dma;
    unsigned map_size = info->fix.smem_len;
    DEBUG_LOG("Enter!");

    info->screen_base = dma_alloc_wc(info->device, PAGE_ALIGN(map_size), &map_dma, GFP_KERNEL);
    if (info->screen_base) {
        /* prevent initial garbage on screen */
        memset(info->screen_base, 0x00, map_size);
        info->fix.smem_start = map_dma;

        DEBUG_LOG("map_video_memory: dma=%08lx cpu=%p size=%08x\n",
            info->fix.smem_start, info->screen_base, map_size);
    }
    return info->screen_base ? 0 : -ENOMEM;
}

static inline void stm32mp157_unmap_video_memory(struct fb_info *info)
{
    if (!info) {
        DEBUG_LOG("Invalid fb_info!");
        return;
    }
    DEBUG_LOG("Enter!");
    dma_free_wc(info->device, PAGE_ALIGN(info->fix.smem_len),
            info->screen_base, info->fix.smem_start);
}

int stm32mp157_lcd_probe(struct platform_device *pdev)
{
    struct device_node	*pn = NULL;
    const char *dev_name = NULL;
    u32 xres, yres, bpp;
    int err = 0;

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
    // stm_fbinfo.dev = &(pdev->dev);

    // 从设备树中获取参数
    of_property_read_u32(pn, "xres", &xres);
    of_property_read_u32(pn, "yres", &yres);
    of_property_read_u32(pn, "bpp", &bpp);
    of_property_read_string(pn, "label", &dev_name);
    DEBUG_LOG("get device node: name=%s, path=%s, id=%d", pn->name, pn->full_name, pn->phandle);
    DEBUG_LOG("get info: name=%s, xres=%d, yres=%d, bpp=%d", dev_name, xres, yres, bpp);

    // 分配fbinfo
    g_fbinfo = framebuffer_alloc(0, &(pdev->dev));
    if (!g_fbinfo) {
        DEBUG_LOG("Alloc framebuffer memory failed!");
        return -ENOMEM;
    }

    // 设置fbinfo
    /* 设置可变参数 */
    g_fbinfo->var.xres = xres;
    g_fbinfo->var.yres = yres;
    g_fbinfo->var.xres_virtual = xres;
    g_fbinfo->var.yres_virtual = yres;
    g_fbinfo->var.bits_per_pixel = bpp;
    if (bpp == 24) {
        g_fbinfo->var.red.length = 8;
        g_fbinfo->var.red.offset = 16;
        g_fbinfo->var.green.length = 8;
        g_fbinfo->var.green.offset = 8;
        g_fbinfo->var.blue.length = 8;
        g_fbinfo->var.blue.offset = 0;
    } else {
        // rgb 565
        g_fbinfo->var.red.length = 5;
        g_fbinfo->var.red.offset = 11;
        g_fbinfo->var.green.length = 6;
        g_fbinfo->var.green.offset = 5;
        g_fbinfo->var.blue.length = 5;
        g_fbinfo->var.blue.offset = 0;
    }
    // g_fbinfo->var.nonstd	    = 0;
    // g_fbinfo->var.activate	    = FB_ACTIVATE_NOW;
    // g_fbinfo->var.accel_flags     = 0;
    // g_fbinfo->var.vmode	    = FB_VMODE_NONINTERLACED;

    /* 设置固定参数 */
    if (bpp == 24) {
        g_fbinfo->fix.smem_len = xres * yres * 4;
        g_fbinfo->fix.line_length = xres * 4;
    } else {
        g_fbinfo->fix.smem_len = (xres * yres * bpp) / 8;
        g_fbinfo->fix.line_length = (xres * bpp) / 8;
    }
    strncpy(g_fbinfo->fix.id, dev_name, MAX_LCD_NAME_LEN);
    g_fbinfo->fix.id[MAX_LCD_NAME_LEN - 1] = 0;
    g_fbinfo->fix.type	    = FB_TYPE_PACKED_PIXELS;
    g_fbinfo->fix.type_aux	    = 0;
    g_fbinfo->fix.xpanstep	    = 0;
    g_fbinfo->fix.ypanstep	    = 0;
    g_fbinfo->fix.ywrapstep	    = 0;
    g_fbinfo->fix.accel	    = FB_ACCEL_NONE;

    /* 设置显存的虚拟地址和物理地址 */
    if (stm32mp157_map_video_memory(g_fbinfo)) {
        DEBUG_LOG("Alloc vedio memory failed!");
        err = -ENOMEM;
        goto fbinfo_free;
    }

    /* 设置fb_ops */
    g_fbinfo->fbops = &stm32_fb_ops;

    /* 其它设置 */
    g_fbinfo->flags = FBINFO_FLAG_DEFAULT;
    g_fbinfo->pseudo_palette = kmalloc_array(16, sizeof(u32), GFP_KERNEL);

    // 向fbmem.c注册fbinfo
    err = register_framebuffer(g_fbinfo);
    if (err < 0) {
        DEBUG_LOG("Failed to register framebuffer device: %d\n", err);
        goto vmem_free;
    }
    return 0;

// fb_info_unreg:
//     if (g_fbinfo)
//         unregister_framebuffer(g_fbinfo);

vmem_free:
    if (g_fbinfo)
        stm32mp157_unmap_video_memory(g_fbinfo);

fbinfo_free:
    if (g_fbinfo)
        framebuffer_release(g_fbinfo);
    return err;
}

int stm32mp157_lcd_remove(struct platform_device *pdev)
{
    DEBUG_LOG("Enter!");
    // 向fbmem.c反注册fbinfo
    if (g_fbinfo)
        unregister_framebuffer(g_fbinfo);
    
    // 释放pseudo_palette
    DEBUG_LOG("Free pseudo_palette!");
    if (g_fbinfo->pseudo_palette)
        kfree(g_fbinfo->pseudo_palette);

    // 释放显存
    DEBUG_LOG("Free framebuffer!");
    if (g_fbinfo)
        stm32mp157_unmap_video_memory(g_fbinfo);

    // 释放fbinfo
    DEBUG_LOG("Free fb_info!");
    if (g_fbinfo)
        framebuffer_release(g_fbinfo);
    DEBUG_LOG("end!");
    return 0;
}

static const struct of_device_id ask100_lcd_of_match[] = {
    { .compatible = "100ask,lcd_dtb_drv", },
    {},
};

static struct platform_driver stm32mp157_lcd_driver = {
    .probe		= stm32mp157_lcd_probe,
    .remove		= stm32mp157_lcd_remove,
    .driver		= {
        .name	= "100ask_fb",
        .of_match_table = ask100_lcd_of_match,
    },
};

module_platform_driver(stm32mp157_lcd_driver);
