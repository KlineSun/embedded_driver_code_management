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


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kline <kline_code@example,com>");
MODULE_DESCRIPTION("Test for virtual spi master driver.");

#define DEBUG_LOG(fmt, ...) \
        printk(KERN_INFO "[%s line%d] %s(): "fmt"\n", \
            KBUILD_BASENAME, __LINE__, __FUNCTION__, ##__VA_ARGS__)

#define OLED_BPP (1)

struct virt_spi_ctrl {
    struct spi_master *master;
    spinlock_t lock;
    struct device *parent_dev;
    struct work_struct work;
    unsigned msg_cnt;
    struct list_head queue; // 等待传输的消息队列
};

static struct virt_spi_ctrl *g_virt_spi_ctrl = NULL;

// static int virt_spi_transfer_one(struct spi_master *master,
//                     struct spi_device *spi_dev,
//                     struct spi_transfer *transfer)
// {
//     DEBUG_LOG("Enter!");

//     return 0;
// }

static void virt_spi_spi_wq(struct work_struct *work)
{
    struct virt_spi_ctrl *virt_ms = container_of(work, struct virt_spi_ctrl, work);
    unsigned long flags;
    struct spi_message *mesg, *tmp;

    spin_lock_irqsave(&virt_ms->lock, flags);

    // 逐个传输队列中的数据
    list_for_each_entry_safe(mesg, tmp, &virt_ms->queue, queue) {
        DEBUG_LOG("message%u transfer!", virt_ms->msg_cnt);

        // 在这里完成spi数据的硬件传输

        // 传输完成后将status置为0
        mesg->status = 0;
        virt_ms->msg_cnt++;
        list_del_init(&mesg->queue);

        // 唤醒异步等待的进程
        // 解锁后调用complete
        spin_unlock_irqrestore(&virt_ms->lock, flags);
        if (mesg->complete)
            mesg->complete(mesg->context);
        spin_lock_irqsave(&virt_ms->lock, flags);
    }

    spin_unlock_irqrestore(&virt_ms->lock, flags);
}

int virt_spi_transfer(struct spi_device *spi, struct spi_message *mesg)
{
    /**
     * 异步传输的transfer方法
     * 
     * 调用路径：__spi_sync -> spi_async_locked -> __spi_async -> transfer回调函数
     * 
     * 在transfer函数中实现以下操作：
     *      - 把当前要传输的spi_message的transfer链表mesg->queue加入到master的queue中去
     *      - 唤醒一个工作队列schedule_work
     * 在工作队列中实现以下操作：
     *      - 遍历当前master中存储的message
     *      - 逐个发送spi数据
     *      - 将message的status置为0
     * 
     * 参考代码： drivers\spi\spi-mpc52xx.c
    */
    struct virt_spi_ctrl *virt_ms = spi_master_get_devdata(spi->master);
    unsigned long flags;
    if (!virt_ms) {
        DEBUG_LOG("Get spi devdata failed!");
        return -EINVAL;
    }
    DEBUG_LOG("Enter!");
    // 将message status置为正在处理
    mesg->status = -EINPROGRESS;

    // 将队列添加到master的队列中去
    spin_lock_irqsave(&virt_ms->lock, flags);
    list_add_tail(&mesg->queue, &virt_ms->queue);
    spin_unlock_irqrestore(&virt_ms->lock, flags);

    // 调度工作队列发送spi message
    DEBUG_LOG("schedule workqueue!");
    schedule_work(&virt_ms->work);
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
    g_virt_spi_ctrl->master = master;
    // 初始化异步传输时的工作队列、锁和消息队列
    INIT_WORK(&g_virt_spi_ctrl->work, virt_spi_spi_wq);
    spin_lock_init(&g_virt_spi_ctrl->lock);
    INIT_LIST_HEAD(&g_virt_spi_ctrl->queue);
    g_virt_spi_ctrl->parent_dev = dev;

    // 设置spi_master
    master->num_chipselect = 2;
	master->dev.of_node = pdev->dev.of_node;
    // master->transfer_one = virt_spi_transfer_one; // 新的spi传输方法
    master->transfer = virt_spi_transfer; // 老的spi传输方法

    // 注册spi_master
    err = spi_register_master(master);
	if (err) {
		DEBUG_LOG("spi master registration failed");
		goto put_spi_master;
	}

    DEBUG_LOG("Probe button success!");
    return 0;

put_spi_master:
    spi_master_put(master);
    return err;
}

static int virt_spi_master_remove(struct platform_device *pdev)
{
    DEBUG_LOG("Enter!");

    // 反注册spi_master
    if (g_virt_spi_ctrl->master)
        spi_unregister_master(g_virt_spi_ctrl->master);
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
