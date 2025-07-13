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
#include <dt-bindings/interrupt-controller/arm-gic.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kline <kline_code@example,com>");
MODULE_DESCRIPTION("Test for virtual interrupt controller.");


#define DEBUG_LOG(fmt, ...) \
        printk(KERN_INFO "[%s line%d] %s(): "fmt"\n", \
            KBUILD_BASENAME, __LINE__, __FUNCTION__, ##__VA_ARGS__)

struct virt_intc_desc {
    struct device *dev;
    unsigned int virq_base; // 使用层级注册时，设备占用gic中断号的基准值
    int nirqs; // virt_intc有多少个中断资源
    struct irq_domain *domain;
};

static struct virt_intc_desc *g_vintc_desc = NULL;

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
    irq_chip_eoi_parent(data);
}

static struct irq_chip virt_intc_irq_chip = {
    .name		= "VIRT_INTC",
    .irq_ack = virt_intc_irq_ack,
    .irq_mask	= virt_intc_irq_mask,
    .irq_mask_ack	= virt_intc_irq_mask_ack,
    .irq_unmask	= virt_intc_irq_unmask,
    .irq_eoi	= virt_intc_irq_eoi,
};

int virt_intc_domain_alloc(struct irq_domain *domain, unsigned int virq,
            unsigned int nr_irqs, void *arg)
{
    struct irq_fwspec *fwspec = arg;
    struct irq_fwspec parent_fwspec;
    irq_hw_number_t hwirq;
    int i;

    if (fwspec->param_count != 2)
        return -EINVAL;

    DEBUG_LOG("Enter!");
    // 明确下层中断号和顶层中断号之间的关系
    hwirq = fwspec->param[0];
    DEBUG_LOG("Get hwirq=%lu, virq=%d, nr_irqs=%d!", hwirq, virq, nr_irqs);
    if (hwirq >= g_vintc_desc->nirqs) {
        DEBUG_LOG("hwirq is out of irq count!");
        return -EINVAL;	/* Can't deal with this */
    }

    // 为每一个从属的irq_desc设置irq_chip
    for (i = 0; i < nr_irqs; i++)
        irq_domain_set_hwirq_and_chip(domain, virq + i, hwirq + i,
                        &virt_intc_irq_chip, NULL);

    parent_fwspec = *fwspec;
    /**
     * 调试经验：
     *      param_count没有合理的设置，导致在下级设备使用platform_irq_count时返回一直为0；
     *      调用platform_irq_count函数时，代码会调用到这里：
     *          - 用中断控制器domain中的translate来解析设备树中的中断描述参数
     *          - 用中断控制器domain中的alloc来为上层中断控制器(即GIC)提供virq和hwirq的对应关系
    */
    parent_fwspec.param_count = 3;
    parent_fwspec.fwnode = domain->parent->fwnode;
    parent_fwspec.param[0] = GIC_SPI; // 中断类型,SPI、PPI等
    // 中断号，这里的virq是在虚拟中断控制器上的中断号，映射到gic上，需要加上gic上的基准值
    parent_fwspec.param[1] = hwirq + g_vintc_desc->virq_base;
    parent_fwspec.param[2] = IRQ_TYPE_LEVEL_HIGH; // 触发类型
    return irq_domain_alloc_irqs_parent(domain, virq, nr_irqs,
                        &parent_fwspec);
}

int virt_intc_domain_translate(struct irq_domain *d, struct irq_fwspec *fwspec,
            unsigned long *out_hwirq, unsigned int *out_type)
{

    DEBUG_LOG("Enter with param_count: %d!", fwspec->param_count);

    // 解释虚拟中断控制器中的参数，确定其含义
    if (is_of_node(fwspec->fwnode)) {
        // cells为2
        if (fwspec->param_count != 2) {
            DEBUG_LOG("Invalid parameter count!");
            return -EINVAL;
        }

        *out_hwirq = fwspec->param[0];
        *out_type = fwspec->param[1];
        return 0;
    }
    return -EINVAL;
}

static const struct irq_domain_ops virt_intc_irq_domain_ops = {
    .alloc = virt_intc_domain_alloc,
    .translate = virt_intc_domain_translate,
};

int virtual_intc_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    struct device_node	*np = pdev->dev.of_node;
    struct irq_domain *parent_domain = NULL;
    struct device_node *parent = NULL;

    if (!dev || !np) {
        DEBUG_LOG("Invalid device node!");
        return -EINVAL;
    }

    DEBUG_LOG("Enter!");
    // 获取parent的domain
    // parent = of_get_parent(np);
    parent = of_irq_find_parent(np);
    if (!parent) {
        DEBUG_LOG("Get parent node!");
    }
    parent_domain = irq_find_host(parent);
    if (!parent_domain) {
        DEBUG_LOG("%pOF: unable to obtain parent domain\n", parent);
        return -ENXIO;
    }

    // 分配全局结构体g_vintc_desc
    g_vintc_desc = devm_kzalloc(dev, sizeof(*g_vintc_desc), GFP_KERNEL);
    if (!g_vintc_desc) {
        DEBUG_LOG("Invalid device node!");
        return -ENOMEM;
    }

    // 从dts中获取到virt_intc连接到GIC的基准值
    if (of_property_read_u32(np, "upper_hwirq_base", &g_vintc_desc->virq_base)) {
        DEBUG_LOG("Invalid device node!");
        return -EINVAL;
    }

    // 从dts中获取到virt_intc拥有的中断资源数量，赋值给g_irq_res_cnt
    if (of_property_read_u32(np, "nirqs", &g_vintc_desc->nirqs)) {
        DEBUG_LOG("Invalid device node!");
        return -EINVAL;
    }

    // 为这些虚拟中断号创建irq_domain，使用irq_domain_add_hierarchy层级方式，并绑定domain_ops
    g_vintc_desc->domain = irq_domain_add_hierarchy(parent_domain,
                                                    0,
                                                    g_vintc_desc->nirqs,
                                                    np,
                                                    &virt_intc_irq_domain_ops,
                                                    g_vintc_desc);
    if (!g_vintc_desc->domain) {
        DEBUG_LOG("Add irq_domain by legacy failed!");
        return -ENODEV;
    }

    DEBUG_LOG("Register irq_domain success: virq_base=%d, nirqs=%d!", g_vintc_desc->virq_base, g_vintc_desc->nirqs);

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