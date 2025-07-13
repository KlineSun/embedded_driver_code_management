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
#include <linux/irq.h>
#include <asm/io.h>
#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/interrupt.h>
#include <linux/of_irq.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/random.h>
#include <linux/irqdomain.h>
#include <linux/irqdesc.h>
#include <linux/platform_device.h>
#include <dt-bindings/gpio/gpio.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kline <kline_code@example,com>");
MODULE_DESCRIPTION("Test for virtual interrupt controller.");


#define DEBUG_LOG(fmt, ...) \
        printk(KERN_INFO "[%s line%d] %s(): "fmt"\n", \
            KBUILD_BASENAME, __LINE__, __FUNCTION__, ##__VA_ARGS__)


#define get_max_bit_offset(num) ({\
        int i = 0;\
        while ((num >> i) > 0)\
            i++;\
        i - 1;\
    })\

struct virt_intc_desc {
    struct device *dev;
    int virq; // virt_intc的虚拟中断号
    int nirqs; // virt_intc有多少个中断资源
    int child_virq_base; // 使用linear方式不需要实现注册irq_desc，仅在用到的时候注册确认
    struct irq_domain *domain;
};


static struct virt_intc_desc *g_vintc_desc = NULL;

static int get_virt_intc_hwirq(int max)
{
    int bit_mask = (1 << (get_max_bit_offset(max) + 1)) - 1;

    DEBUG_LOG("Enter with bit_mask: 0x%x", bit_mask);
    // 获取小于g_irq_res_cnt的一个随机数作为触发的中断号
    // 实际上这里需要读取硬件来检查是哪个中断发生了
    return  get_random_int() & bit_mask;
}

static void virt_intc_handle_irq(struct irq_desc *desc)
{
    int hwirq = 0;
    struct virt_intc_desc *vintc_desc = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);

    DEBUG_LOG("Enter!");
    // 屏蔽中断，调用chip中的irq_mask函数
    chained_irq_enter(chip, desc);

    // 分辨是发生了虚拟中断控制器下的哪一个中断
    hwirq = get_virt_intc_hwirq(vintc_desc->nirqs - 1);

    // 调用对应irq_desc中的handle_irq函数
    // domain为irq_domain_add_legacy中创建的domain
    generic_handle_irq(irq_find_mapping(vintc_desc->domain, hwirq));

    // 清除中断: 调用chip中的irq_eoi函数; 取消屏蔽: 调用chip中的irq_unmask函数;
    chained_irq_exit(chip, desc);
    DEBUG_LOG("End!");
}

void virt_intc_irq_ack(struct irq_data *data)
{
    DEBUG_LOG("Enter!");

}

void virt_intc_irq_mask(struct irq_data *data)
{
    DEBUG_LOG("Enter!");

}

void virt_intc_irq_mask_ack(struct irq_data *data)
{
    DEBUG_LOG("Enter!");
}

void virt_intc_irq_unmask(struct irq_data *data)
{
    DEBUG_LOG("Enter!");
}

void virt_intc_irq_eoi(struct irq_data *data)
{
    DEBUG_LOG("Enter!");
}

static struct irq_chip virt_intc_irq_chip = {
	.name		= "VIRT_INTC",
    .irq_ack = virt_intc_irq_ack,
    .irq_mask	= virt_intc_irq_mask,
    .irq_mask_ack	= virt_intc_irq_mask_ack,
	.irq_unmask	= virt_intc_irq_unmask,
    .irq_eoi	= virt_intc_irq_eoi,
};

static int virt_intc_domain_map(struct irq_domain *d, unsigned int irq, irq_hw_number_t hw)
{
    // 为从属的几个irq对应的irq_desc，注册irq_chip和handle_irq
    irq_set_chip_and_handler(irq, &virt_intc_irq_chip, handle_level_irq);
    // 为从属的irq注册chip_data，
    irq_set_chip_data(irq, g_vintc_desc);
    return 0;
}

static const struct irq_domain_ops virt_intc_irq_domain_ops = {
    .xlate = irq_domain_xlate_onecell,
    .map = virt_intc_domain_map,
};

int virtual_intc_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    struct device_node	*np = pdev->dev.of_node;

    if (!dev || !np) {
        DEBUG_LOG("Invalid device node!");
        return -EINVAL;
    }

    DEBUG_LOG("Enter!");
    // 分配全局结构体g_vintc_desc
    g_vintc_desc = devm_kzalloc(dev, sizeof(*g_vintc_desc), GFP_KERNEL);
    if (!g_vintc_desc) {
        DEBUG_LOG("Invalid device node!");
        return -ENOMEM;
    }

    // 从dts中获取到virt_intc连接到GIC的哪一号中断
    g_vintc_desc->virq = of_irq_get(np, 0);
    if (g_vintc_desc->virq <= 0) {
        DEBUG_LOG("Invalid device node!");
        return -ENOMEM;
    }

    // 从dts中获取到virt_intc拥有的中断资源数量，赋值给g_irq_res_cnt
    if (of_property_read_u32(np, "nirqs", &g_vintc_desc->nirqs)) {
        DEBUG_LOG("Invalid device node!");
        return -EINVAL;
    }

    // 设置该中断号对应的irq_desc的中断处理函数handle_irq
    irq_set_chained_handler_and_data(g_vintc_desc->virq, virt_intc_handle_irq, g_vintc_desc);

    // 分配/设置/注册domain
    // 在linear方式下无需事先注册irq_desc
    // g_vintc_desc->child_virq_base = devm_irq_alloc_descs(dev,
    //                                                     -1,
    //                                                     0,
    //                                                     g_vintc_desc->nirqs,
    //                                                     numa_node_id());
    // if (g_vintc_desc->child_virq_base < 0) {
    //     DEBUG_LOG("Alloc irq_desc failed!");
    //     return  g_vintc_desc->child_virq_base;
    // }

    // 为这些虚拟中断号创建irq_domain，使用irq_domain_add_linear方式，并绑定domain_ops
    g_vintc_desc->domain = irq_domain_add_linear(np, g_vintc_desc->nirqs, &virt_intc_irq_domain_ops, g_vintc_desc);
    if (!g_vintc_desc->domain) {
        DEBUG_LOG("Add irq_domain by legacy failed!");
        return -ENODEV;
    }

    DEBUG_LOG("Register irq_domain success: virq=%d, nirqs=%d!", g_vintc_desc->virq, g_vintc_desc->nirqs);

    DEBUG_LOG("Probe button success!");
    return 0;
}

int virtual_intc_remove(struct platform_device *pdev)
{
    DEBUG_LOG("Enter!");

    if (g_vintc_desc->domain)
        irq_domain_remove(g_vintc_desc->domain);

    return 0;
}

static const struct of_device_id ask100_virt_intc_of_match[] = {
	{ .compatible = "100ask,virt_intc_drv", },
	{},
};

static struct platform_driver virt_intc_driver = {
	.probe		= virtual_intc_probe,
	.remove		= virtual_intc_remove,
	.driver		= {
		.name	= "100ask_virt_intc",
        .of_match_table = ask100_virt_intc_of_match,
	},
};

module_platform_driver(virt_intc_driver);