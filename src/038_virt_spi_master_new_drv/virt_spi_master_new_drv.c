#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/errno.h>
#include <uapi/asm-generic/errno.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/kmod.h>
#include <asm/io.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/spi/spi_bitbang.h>


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kline <kline_code@example,com>");
MODULE_DESCRIPTION("Test for virtual spi master driver.");

#define DEBUG_LOG(fmt, ...) \
        printk(KERN_INFO "[%s line%d] %s(): "fmt"\n", \
            KBUILD_BASENAME, __LINE__, __FUNCTION__, ##__VA_ARGS__)

#define OLED_BPP (1)

struct virt_spi_ctrl {
    struct spi_bitbang bitbang;
    spinlock_t lock;
    struct device *parent_dev;
    struct spi_master *master;
    struct completion done;
};

static struct virt_spi_ctrl *g_virt_spi_ctrl = NULL;

static int vir_spi_txrx_bufs(struct spi_device *spi, struct spi_transfer *t)
{
    struct virt_spi_ctrl *virt_ms = spi_master_get_devdata(spi->master);

    if (!virt_ms) {
        DEBUG_LOG("Invalid parameter!");
        return -EINVAL;
    }

    DEBUG_LOG("Enter!");
    // reinit done
    reinit_completion(&virt_ms->done);

    // 硬件传输数据
    DEBUG_LOG("spi_transfer into spi device!");

    // 等待硬件返回，一般为中断信号，表示已经接收到信息

    // 这里直接设置complete，模拟在中断中发送完成即可
    msleep(20);
    complete(&virt_ms->done);

    // 等待数据发送完成
    if (wait_for_completion_timeout(&virt_ms->done, HZ) == 0) {
        DEBUG_LOG("spi transfer timeout!");
        return -1;
    }
    return t->len;
}

static void vir_spi_chipsel(struct spi_device *spi, int is_on)
{
    DEBUG_LOG("Enter!");
}


static int vir_spi_setup_transfer(struct spi_device *spi, struct spi_transfer *t)
{
    DEBUG_LOG("Enter!");

    return 0;
}

static int virt_spi_master_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    struct device_node	*pn = pdev->dev.of_node;
    int err = 0;
    struct spi_master *master = NULL;

    if (!dev || !pn) {
        DEBUG_LOG("Invalid device node!");
        return -EINVAL;
    }

    DEBUG_LOG("Enter!");
    // 分配spi_master
    master = spi_alloc_master(dev, sizeof(*g_virt_spi_ctrl));
    if (!master) {
        DEBUG_LOG("Alloc spi_master failed!");
        return -ENOMEM;
    }
    platform_set_drvdata(pdev, master);

    // 获取已分配的全局结构体
    g_virt_spi_ctrl = spi_master_get_devdata(master);
    if (!g_virt_spi_ctrl) {
        DEBUG_LOG("Invalid virt_spi_ctrl pointer!");
        err = -EINVAL;
        goto put_spi_master;
    }
    g_virt_spi_ctrl->bitbang.master = master; // 构建bitbang和spi_master的联系
    // 初始化异步传输时的工作队列、锁和消息队列
    spin_lock_init(&g_virt_spi_ctrl->lock);
    init_completion(&g_virt_spi_ctrl->done);
    g_virt_spi_ctrl->parent_dev = dev;
    g_virt_spi_ctrl->master = master;

    // 设置spi_master
    master->num_chipselect = 2;
    master->dev.of_node = pdev->dev.of_node;

    // 借用bitbang的框架给spi_master提供必要的回调函数
    g_virt_spi_ctrl->bitbang.txrx_bufs = vir_spi_txrx_bufs;
    g_virt_spi_ctrl->bitbang.chipselect = vir_spi_chipsel;
    g_virt_spi_ctrl->bitbang.setup_transfer = vir_spi_setup_transfer;

    // start spi_bitbang
    err = spi_bitbang_start(&g_virt_spi_ctrl->bitbang);

    DEBUG_LOG("Probe button success!");
    return 0;

put_spi_master:
    spi_master_put(master);
    return err;
}

static int virt_spi_master_remove(struct platform_device *pdev)
{
    DEBUG_LOG("Enter!");

    // stop spi_bitbang
    spi_bitbang_stop(&g_virt_spi_ctrl->bitbang);
    spi_master_put(g_virt_spi_ctrl->master);
    return 0;
}

static const struct of_device_id ask100_virt_spi_master_of_match[] = {
	{ .compatible = "100ask,virt_spi_master", },
	{},
};

static struct platform_driver virt_spi_master_driver = {
	.probe		= virt_spi_master_probe,
	.remove		= virt_spi_master_remove,
	.driver		= {
		.name	= "virt_spi_master_drv",
        .of_match_table = ask100_virt_spi_master_of_match,
	},
};

module_platform_driver(virt_spi_master_driver);
