#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/of_device.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/pinctrl/pinconf.h>
#include "virt_pinctrl_drv.h"
#include "core.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kline <kline_code@example,com>");
MODULE_DESCRIPTION("Test for ts input device");

#ifdef IS_PINCTRL_GROUPING
#define MAX_PINS_IN_GROUP (16)
#else
#define MAX_PINS_IN_GROUP (1)
#endif

#define PINMUX_I2C  0
#define PINMUX_GPIO 1
#define PINMUX_UART 2

#define PINMUX_I2C_FLAG     (1 << 0)
#define PINMUX_GPIO_FLAG    (1 << 1)
#define PINMUX_UART_FLAG    (1 << 2)

#define MATCH_ARRAY_ELEM(array, size, member, val, idx) \
{\
    int i = 0; \
    for (i = 0; i < size; i++) {\
        if (!strcmp(array[i].member, val) || array[i].member == val) {\
            idx = i;\
            break;\
        }\
    }\
}\

struct vpctrl_pinmux {
    unsigned num;
    const char *func_name;
    int (*func_set) (struct pinctrl_pin_desc *pin, void *drvdata);
    void *drvdata;
};

struct vpctrl_config {
    const char * const property;
    enum pin_config_param param;
    u32 default_value;
    int (*config_set) (struct pinctrl_pin_desc *pin, void *drvdata);
    void *drvdata;
};

struct virtual_pinctrl_pins {
    struct pinctrl_pin_desc desc;
    u16 mux_flag;   // 支持的复用功能有哪些？
    bool seleted;     // 当前pin脚是否被设备树选中？
    const struct vpctrl_pinmux *mux;      // 当前的复用功能是哪个
    unsigned long *configs;      // 当前的配置有哪些
    unsigned int nconfigs;     // 当前的位图总共有多少位
};

struct vpctrl_groups {
    unsigned num;
    char name[16];
    unsigned pins[MAX_PINS_IN_GROUP];
    unsigned npins;
};

struct virtual_pinctrl_desc {
    struct device *dev;
    struct pinctrl_desc pctl_desc;
    struct pinctrl_dev *pin_dev;
    struct vpctrl_groups *groups; // 有哪些group
    unsigned int ngroups; // group数量
    const char **grp_names;
};

static struct virtual_pinctrl_desc *g_vpinctrl;

static const struct pinctrl_pin_desc my_pins_desc[] = {
    {0, "pin0", NULL},
    {1, "pin1", NULL},
    {2, "pin2", NULL},
    {3, "pin3", NULL},
    {4, "pin4", NULL},
    {5, "pin5", NULL},
};

static struct virtual_pinctrl_pins my_pins[] = {
    {
        .desc = my_pins_desc[0],
        .mux_flag = PINMUX_I2C_FLAG | PINMUX_GPIO_FLAG,
    },
    {
        .desc = my_pins_desc[1],
        .mux_flag = PINMUX_I2C_FLAG | PINMUX_GPIO_FLAG,
    },
    {
        .desc = my_pins_desc[2],
        .mux_flag = PINMUX_I2C_FLAG | PINMUX_GPIO_FLAG,
    },
    {
        .desc = my_pins_desc[3],
        .mux_flag = PINMUX_I2C_FLAG | PINMUX_GPIO_FLAG,
    },
    {
        .desc = my_pins_desc[4],
        .mux_flag = PINMUX_UART_FLAG | PINMUX_GPIO_FLAG,
    },
    {
        .desc = my_pins_desc[5],
        .mux_flag = PINMUX_UART_FLAG | PINMUX_GPIO_FLAG,
    },
};

static int build_vptrl_group_tab(struct virtual_pinctrl_desc *vpctrl)
{
    int npins = vpctrl->pctl_desc.npins;
    int i = 0, j = 0, ngroups = 0;

    if (!vpctrl || npins <= 0) {
        DEBUG_LOG("Invalid parameter!");
        return -1;
    }

    DEBUG_LOG("Enter!");
    DEBUG_LOG("Number of pin: %d", npins);

#ifdef IS_PINCTRL_GROUPING
    ngroups = npins / MAX_PINS_IN_GROUP;
    if ((ngroups * MAX_PINS_IN_GROUP) < npins)
        ngroups++;
#else
    ngroups = npins;

#endif

    DEBUG_LOG("Number of group: %d", ngroups);
    vpctrl->groups = devm_kzalloc(vpctrl->dev, sizeof(struct vpctrl_groups) * ngroups, GFP_KERNEL);
    if (!vpctrl->groups) {
        DEBUG_LOG("Alloc groups memory failed!");
        return -1;
    }

    vpctrl->grp_names = devm_kzalloc(vpctrl->dev, sizeof(*vpctrl->grp_names) * ngroups, GFP_KERNEL);
    if (!vpctrl->grp_names) {
        DEBUG_LOG("Alloc groups memory failed!");
        return -1;
    }

    for (i = 0; i < ngroups; i++) {
        vpctrl->groups[i].num = i;
        //set group name
    #ifdef IS_PINCTRL_GROUPING
        ngroups = npins / MAX_PINS_IN_GROUP;
        if ((ngroups * MAX_PINS_IN_GROUP) < npins)
            ngroups++;
        snprintf(vpctrl->groups[i].name, 16, "group%d", i);
    #else
        snprintf(vpctrl->groups[i].name, 16, "%s", my_pins_desc[i].name);
    #endif
        for (j = 0; j < MAX_PINS_IN_GROUP; j++) {
            // set group pins
            vpctrl->groups[i].pins[j] = my_pins_desc[i*MAX_PINS_IN_GROUP + j].number;
            vpctrl->groups[i].npins++;
            printk("Group%d: %s,    Pin%d: %s \n", i, vpctrl->groups[i].name, j, my_pins_desc[i*MAX_PINS_IN_GROUP + j].name);
        }
        vpctrl->grp_names[i] = vpctrl->groups[i].name;
        vpctrl->ngroups++;
    }

    DEBUG_LOG("End!");
    return 0;
}

int generic_vpctrl_pinmux_set(struct pinctrl_pin_desc *pin, void *drvdata)
{
    const char *pinmux = drvdata;

    if (!pinmux || !pin) {
        DEBUG_LOG("Invalid parameter!");
        return -1;
    }
    DEBUG_LOG("Enter!");
    // 硬件操作设置为对应的pinmux模式

    return 0;
}

static const struct vpctrl_pinmux vpctrl_pinmux_table[] = {
    {PINMUX_I2C, "i2c", generic_vpctrl_pinmux_set, "i2c"},
    {PINMUX_GPIO, "gpio", generic_vpctrl_pinmux_set, "gpio"},
    {PINMUX_UART, "uart", generic_vpctrl_pinmux_set, "uart"},
};

int generic_vpctrl_pinconf_set(struct pinctrl_pin_desc *pin, void *drvdata)
{
    if (!drvdata || !pin) {
        DEBUG_LOG("Invalid parameter!");
        return -1;
    }

    DEBUG_LOG("Enter!");
    // 根据传入的config进行硬件设置

    return 0;
}


static const struct pinconf_generic_params local_dt_params[] = {
	{ "bias-bus-hold", PIN_CONFIG_BIAS_BUS_HOLD, 0 },
	{ "bias-disable", PIN_CONFIG_BIAS_DISABLE, 0 },
	{ "bias-high-impedance", PIN_CONFIG_BIAS_HIGH_IMPEDANCE, 0 },
	{ "bias-pull-up", PIN_CONFIG_BIAS_PULL_UP, 1 },
	{ "bias-pull-pin-default", PIN_CONFIG_BIAS_PULL_PIN_DEFAULT, 1 },
	{ "bias-pull-down", PIN_CONFIG_BIAS_PULL_DOWN, 1 },
	{ "drive-open-drain", PIN_CONFIG_DRIVE_OPEN_DRAIN, 0 },
	{ "drive-open-source", PIN_CONFIG_DRIVE_OPEN_SOURCE, 0 },
	{ "drive-push-pull", PIN_CONFIG_DRIVE_PUSH_PULL, 0 },
	{ "drive-strength", PIN_CONFIG_DRIVE_STRENGTH, 0 },
	{ "drive-strength-microamp", PIN_CONFIG_DRIVE_STRENGTH_UA, 0 },
	{ "input-debounce", PIN_CONFIG_INPUT_DEBOUNCE, 0 },
	{ "input-disable", PIN_CONFIG_INPUT_ENABLE, 0 },
	{ "input-enable", PIN_CONFIG_INPUT_ENABLE, 1 },
	{ "input-schmitt", PIN_CONFIG_INPUT_SCHMITT, 0 },
	{ "input-schmitt-disable", PIN_CONFIG_INPUT_SCHMITT_ENABLE, 0 },
	{ "input-schmitt-enable", PIN_CONFIG_INPUT_SCHMITT_ENABLE, 1 },
	{ "low-power-disable", PIN_CONFIG_LOW_POWER_MODE, 0 },
	{ "low-power-enable", PIN_CONFIG_LOW_POWER_MODE, 1 },
	{ "output-disable", PIN_CONFIG_OUTPUT_ENABLE, 0 },
	{ "output-enable", PIN_CONFIG_OUTPUT_ENABLE, 1 },
	{ "output-high", PIN_CONFIG_OUTPUT, 1, },
	{ "output-low", PIN_CONFIG_OUTPUT, 0, },
	{ "power-source", PIN_CONFIG_POWER_SOURCE, 0 },
	{ "sleep-hardware-state", PIN_CONFIG_SLEEP_HARDWARE_STATE, 0 },
	{ "slew-rate", PIN_CONFIG_SLEW_RATE, 0 },
	{ "skew-delay", PIN_CONFIG_SKEW_DELAY, 0 },
};

static const char *match_property_by_param(unsigned int param)
{
    int i = 0;
    if (param > PIN_CONFIG_MAX) {
        DEBUG_LOG("Invalid parameter!");
        return NULL;
    }

    for (i = 0; i < ARRAY_SIZE(local_dt_params); i++) {
        if (param == local_dt_params[i].param)
            return local_dt_params[i].property;
    }

    DEBUG_LOG("Not find param: %d", param);
    return NULL;
}

static void local_parse_dt_cfg(struct device_node *np,
            const struct pinconf_generic_params *params,
            unsigned int count, unsigned long *cfg,
            unsigned int *ncfg)
{
    int i;

    for (i = 0; i < count; i++) {
        u32 val;
        int ret;
        const struct pinconf_generic_params *par = &params[i];

        ret = of_property_read_u32(np, par->property, &val);

        /* property not found */
        if (ret == -EINVAL)
            continue;

        /* use default value, when no value is specified */
        if (ret)
            val = par->default_value;

        DEBUG_LOG("found %s with value %u\n", par->property, val);
        cfg[*ncfg] = pinconf_to_config_packed(par->param, val);
        (*ncfg)++;
    }
}

static int local_pinconf_generic_parse_dt_config(struct device_node *np,
                    struct pinctrl_dev *pctldev,
                    unsigned long **configs,
                    unsigned int *nconfigs)
{
    unsigned long *cfg;
    unsigned int max_cfg, ncfg = 0;
    int ret;

    if (!np)
        return -EINVAL;

    /* allocate a temporary array big enough to hold one of each option */
    max_cfg = ARRAY_SIZE(local_dt_params);
    if (pctldev)
        max_cfg += pctldev->desc->num_custom_params;
    cfg = kcalloc(max_cfg, sizeof(*cfg), GFP_KERNEL);
    if (!cfg)
        return -ENOMEM;

    local_parse_dt_cfg(np, local_dt_params, ARRAY_SIZE(local_dt_params), cfg, &ncfg);
    if (pctldev && pctldev->desc->num_custom_params &&
        pctldev->desc->custom_params)
        local_parse_dt_cfg(np, pctldev->desc->custom_params,
                pctldev->desc->num_custom_params, cfg, &ncfg);

    ret = 0;

    /* no configs found at all */
    if (ncfg == 0) {
        *configs = NULL;
        *nconfigs = 0;
        goto out;
    }

    /*
    * Now limit the number of configs to the real number of
    * found properties.
    */
    *configs = kmemdup(cfg, ncfg * sizeof(unsigned long), GFP_KERNEL);
    if (!*configs) {
        ret = -ENOMEM;
        goto out;
    }

    *nconfigs = ncfg;

out:
    kfree(cfg);
    return ret;
}

static int local_virt_pctrl_dt_node_to_map(struct pinctrl_dev *pctldev,
                struct device_node *np_config,
                struct pinctrl_map **map, unsigned *num_maps)
{
    int err = 0, i = 0, j = 0, idx = -1;
    // 从dt中获取到的数据
    const char *group = NULL;
    unsigned int ngroups = 0;
    const char *function = NULL;

    // 转化之后的config数据
    unsigned long *configs = NULL;
    unsigned int nconfigs = 0;
    unsigned long *dup_configs;

    // 用于记录function 和 config的map
    struct pinctrl_map *maps = NULL;
    unsigned int nmaps = 0;

    struct virtual_pinctrl_desc *vpctrl_desc = pinctrl_dev_get_drvdata(pctldev);
    if (!vpctrl_desc) {
        DEBUG_LOG("Get drvdata failed!");
        return -EINVAL;
    }

    DEBUG_LOG("Enter!");
    // 从dt文件中获取到functions和groups
    ngroups = of_property_count_strings(np_config, "groups");
    if (ngroups <= 0) {
        DEBUG_LOG("Get ngroups failed!");
        return -EINVAL;
    }
    DEBUG_LOG("Get ngroups: %d", ngroups);

    // 获取pinconf等信息
    err = local_pinconf_generic_parse_dt_config(np_config, pctldev, &configs, &nconfigs);
    if (err < 0) {
        DEBUG_LOG("Parse pconf failed!");
        return err;
    }
    DEBUG_LOG("Found pinctrl %d configs: ", nconfigs);
    for (i = 0; i < nconfigs; i++)
        printk("pconf[%d]: %lx\n", i, configs[i]);


    // 为每个group创建两个map，分别用来保存function和pinconf
    nmaps = ngroups * 2;
    maps = devm_kzalloc(vpctrl_desc->dev, sizeof(struct pinctrl_map) * nmaps, GFP_KERNEL);
    if (!maps) {
        DEBUG_LOG("Alloc maps failed!");
        return -ENOMEM;
    }

    // config, 把所有的configs集中到一个dup_configs中
    dup_configs = kmemdup(configs, nconfigs*sizeof(*configs), GFP_KERNEL);
    if (!dup_configs) {
        DEBUG_LOG("Allocate pconf memory failed!");
        return -ENOMEM;
    }

    // 保存function、pinconf到map中
    for (i = 0; i < ngroups; i++) {
        // group and function
        err = of_property_read_string_index(np_config, "groups", i, &group);
        err |= of_property_read_string_index(np_config, "functions", i, &function);
        if (err < 0) {
            DEBUG_LOG("Get functions failed!");
            kfree(dup_configs);
            return err;
        }

        // 找到group对应的pin脚，并记录config值；
        MATCH_ARRAY_ELEM(vpctrl_desc->groups, vpctrl_desc->ngroups, name, group, idx);
        if (idx >= 0) {
            // 遍历group下的所有pin，给其设置configs
            for (j = 0; j < vpctrl_desc->groups[idx].npins; j++) {
                int k = vpctrl_desc->groups[idx].pins[j];
                my_pins[k].configs = dup_configs;
                my_pins[k].nconfigs = nconfigs;
                DEBUG_LOG("Set %d configs for pin%d", nconfigs, k);
            }
        }

        // 记录当前group的PIN_MAP_TYPE_MUX_GROUP类型数据
        maps[2*i].type = PIN_MAP_TYPE_MUX_GROUP;
        maps[2*i].data.mux.group = group;
        maps[2*i].data.mux.function = function;
        DEBUG_LOG("Build map%d success!", 2*i);

        // 记录当前group的PIN_MAP_TYPE_MUX_GROUP类型数据
        maps[2*i + 1].type = PIN_MAP_TYPE_CONFIGS_GROUP;
        maps[2*i + 1].data.configs.group_or_pin = group;
        maps[2*i + 1].data.configs.configs = dup_configs; // 所有group共用一个configs
        maps[2*i + 1].data.configs.num_configs = nconfigs;
        DEBUG_LOG("Build map%d success!", 2*i + 1);
    }

    *map = maps;
    *num_maps = nmaps;
    DEBUG_LOG("End!");
    return 0;
}

void local_virt_pctrl_free_map(struct pinctrl_dev *pctldev, struct pinctrl_map *maps, unsigned num_maps)
{
    int i;

    if (!maps || num_maps <= 0) {
        DEBUG_LOG("Invalid parameter!");
        return;
    }

    DEBUG_LOG("Enter!");
    for (i = 0; i < num_maps; i++) {
        if (maps[i].type == PIN_MAP_TYPE_CONFIGS_GROUP && maps[i].data.configs.configs) {
            kfree(maps[i].data.configs.configs);
            // 所有group共用一个configs，仅需释放一次
            break;
        }
    }
}

int local_virt_pctrl_get_groups_count(struct pinctrl_dev *pctldev)
{
    struct virtual_pinctrl_desc *vpctrl_desc = pinctrl_dev_get_drvdata(pctldev);

    if (!vpctrl_desc) {
        DEBUG_LOG("Get drvdata failed!");
        return -1;
    }
    DEBUG_LOG("Enter!");
    return vpctrl_desc->ngroups;
}


const char *local_virt_pctrl_get_group_name(struct pinctrl_dev *pctldev,
                    unsigned selector)
{
    struct virtual_pinctrl_desc *vpctrl_desc = pinctrl_dev_get_drvdata(pctldev);
    if (!vpctrl_desc) {
        DEBUG_LOG("Get drvdata failed!");
        return NULL;
    }

    DEBUG_LOG("Enter!");

    return vpctrl_desc->groups[selector].name;
}

int local_virt_pctrl_get_group_pins(struct pinctrl_dev *pctldev, unsigned selector, const unsigned **pins, unsigned *num_pins)
{
    int i = 0;
    struct virtual_pinctrl_desc *vpctrl_desc = pinctrl_dev_get_drvdata(pctldev);
    if (!vpctrl_desc) {
        DEBUG_LOG("Get drvdata failed!");
        return -1;
    }

    DEBUG_LOG("Enter!");

    *num_pins = vpctrl_desc->groups[selector].npins;
    if (*num_pins <= 0) {
        DEBUG_LOG("Get invalid groups info!");
        return -1;
    }

    for (i = 0; i < *num_pins; i++) {
        pins[i] = &(vpctrl_desc->groups[selector].pins[i]);
    }

    return 0;
}

static const struct pinctrl_ops local_virt_pctrl_ops = {
    .dt_node_to_map		= local_virt_pctrl_dt_node_to_map,
    .dt_free_map		= local_virt_pctrl_free_map,
    .get_groups_count	= local_virt_pctrl_get_groups_count,
    .get_group_name		= local_virt_pctrl_get_group_name,
    .get_group_pins		= local_virt_pctrl_get_group_pins,
};

int local_virt_pmx_get_funcs_cnt(struct pinctrl_dev *pctldev)
{
    DEBUG_LOG("Enter!");

    return ARRAY_SIZE(vpctrl_pinmux_table);
}

const char *local_virt_pmx_get_func_name(struct pinctrl_dev *pctldev,
                    unsigned selector)
{
    DEBUG_LOG("Enter!");

    return vpctrl_pinmux_table[selector].func_name;
}
int local_virt_pmx_get_func_groups(struct pinctrl_dev *pctldev,
                unsigned selector,
                const char * const **groups,
                unsigned *num_groups)
{
    struct virtual_pinctrl_desc *vpctrl_desc = pinctrl_dev_get_drvdata(pctldev);
    if (!vpctrl_desc) {
        DEBUG_LOG("Get drvdata failed!");
        return -1;
    }

    DEBUG_LOG("Enter!");

    *num_groups = vpctrl_desc->ngroups;
    DEBUG_LOG("num_groups = %d", *num_groups);
    *groups = vpctrl_desc->grp_names;

    return 0;
}

static bool check_pin_func_valid(struct virtual_pinctrl_pins *pin, const struct vpctrl_pinmux *pinmux)
{
    int pin_mux_flag = 0, bit_offset = 0;
    if (!pin || !pinmux) {
        DEBUG_LOG("Invalid parameter!");
        return false;
    }

    pin_mux_flag = pin->mux_flag;
    bit_offset = pinmux->num;
    if (pin_mux_flag & (1 << bit_offset)) {
        return true;
    }

    return false;
}

int local_virt_pmx_set_mux(struct pinctrl_dev *pctldev, unsigned func_selector,
        unsigned group_selector)
{
    int npins = 0, pin_num = 0, i = 0;
    struct virtual_pinctrl_desc *vpctrl_desc = pinctrl_dev_get_drvdata(pctldev);
    const struct vpctrl_pinmux *mux_func = NULL;
    struct virtual_pinctrl_pins *pin = NULL;
    if (!vpctrl_desc) {
        DEBUG_LOG("Get drvdata failed!");
        return -1;
    }

    DEBUG_LOG("Enter!");
    npins = vpctrl_desc->groups[group_selector].npins;
    mux_func = &vpctrl_pinmux_table[func_selector];
    for (i = 0; i < npins; i++) {
        // group内第i个pin的编号
        pin_num = vpctrl_desc->groups[group_selector].pins[i];
        pin = &my_pins[pin_num];

        if (!check_pin_func_valid(pin, mux_func)) {
            DEBUG_LOG("Pin%d unsupport funciton: %s!", pin_num, mux_func->func_name);
            continue;
        }

        // 设置pin脚标志和功能，已选择但未配置
        pin->seleted = true;
        pin->mux = mux_func;
        DEBUG_LOG("Set pin%d as function %s", pin_num, mux_func->func_name);
    }

    return 0;
}

static const struct pinmux_ops local_virt_pmx_ops = {
    .get_functions_count	= local_virt_pmx_get_funcs_cnt,
    .get_function_name	= local_virt_pmx_get_func_name,
    .get_function_groups	= local_virt_pmx_get_func_groups,
    .set_mux		= local_virt_pmx_set_mux,
    //.strict			= false, //true,
};



int local_virt_pconf_set(struct pinctrl_dev *pctldev,
                unsigned pin,
                unsigned long *configs,
                unsigned num_configs)
{
    int i = 0;

    if (pin >= ARRAY_SIZE(my_pins)) {
        DEBUG_LOG("Invalid parameter!");
        return -1;
    }

    DEBUG_LOG("Enter!");

    if (num_configs < my_pins[pin].nconfigs)
        my_pins[pin].nconfigs = num_configs;

    // 根据传入的配置操作硬件设置pin脚
    for ( i = 0; i < my_pins[pin].nconfigs; i++ ) {
        if (configs[i] >= ARRAY_SIZE(local_dt_params)) {
            DEBUG_LOG("Invalid config: %ld", configs[i]);
            return -1;
        }
        DEBUG_LOG("Set config%ld: %s", configs[i], local_dt_params[configs[i]].property);
        my_pins[pin].configs[i] = configs[i];
    }

    return 0;
}

int local_virt_pconf_get(struct pinctrl_dev *pctldev,
                unsigned pin,
                unsigned long *config)
{
    if (pin >= ARRAY_SIZE(my_pins)) {
        DEBUG_LOG("Invalid parameter!");
        return -1;
    }
    DEBUG_LOG("Enter!");

    *config = my_pins[pin].configs[0];

    return 0;
}

void local_virt_pconf_dbg_show(struct pinctrl_dev *pctldev,
                    struct seq_file *s,
                    unsigned offset)
{
    int i = 0;
    DEBUG_LOG("Enter!");

    for (i = 0; i < my_pins[offset].nconfigs; i++) {
        seq_printf(s, "%s ", match_property_by_param(my_pins[offset].configs[i]));
    }
}

void local_virt_pconff_group_dbg_show(struct pinctrl_dev *pctldev,
                    struct seq_file *s,
                    unsigned selector)
{
    int i = 0, j = 0, pin_num = 0;
    struct virtual_pinctrl_desc *vpctrl_desc = pinctrl_dev_get_drvdata(pctldev);
    if (!vpctrl_desc || selector >= vpctrl_desc->ngroups) {
        DEBUG_LOG("Get drvdata failed!");
        return;
    }

    DEBUG_LOG("Enter!");

    seq_printf(s, "group(%s): \n", vpctrl_desc->groups[selector].name);
    for (i = 0; i < vpctrl_desc->groups[selector].npins; i++) {
        pin_num = vpctrl_desc->groups[selector].pins[i];
        seq_printf(s, " pin(%s) configs: ", my_pins_desc[pin_num].name);

        for (j = 0; j < my_pins[pin_num].nconfigs; j++) {
            seq_printf(s, " %s", match_property_by_param(my_pins[pin_num].configs[j]));
        }

        seq_printf(s, ",\n");
    }
}

int local_virt_pconf_group_set(struct pinctrl_dev *pctldev,
                    unsigned selector,
                    unsigned long *configs,
                    unsigned num_configs)
{
    int i = 0, pin_num = 0;
    struct virtual_pinctrl_pins *pin = NULL;
    struct vpctrl_groups *group = NULL;
    struct virtual_pinctrl_desc *vpctrl_desc = pinctrl_dev_get_drvdata(pctldev);
    if (!vpctrl_desc || selector >= vpctrl_desc->ngroups) {
        DEBUG_LOG("Get drvdata failed!");
        return -1;
    }

    DEBUG_LOG("Enter!");

    group = &(vpctrl_desc->groups[selector]);
    for (i = 0; i < group->npins; i++) {
        pin_num = group->pins[i];
        pin = &(my_pins[pin_num]);
        if (!pin) {
            DEBUG_LOG("Invalid pin_desc!");
            return -1;
        }

        pin->configs = configs;
        pin->nconfigs = num_configs;
    }
    return 0;
}

int local_virt_pconf_group_get(struct pinctrl_dev *pctldev,
				     unsigned selector,
				     unsigned long *config)
{
    struct vpctrl_groups *group = NULL;
    struct virtual_pinctrl_desc *vpctrl_desc = pinctrl_dev_get_drvdata(pctldev);
    if (!vpctrl_desc || selector >= vpctrl_desc->ngroups) {
        DEBUG_LOG("Get drvdata failed!");
        return -1;
    }
    DEBUG_LOG("Enter!");

    group = &(vpctrl_desc->groups[selector]);
    // 固定返回第一个pin脚的配置
    *config = my_pins[group->pins[0]].configs[0];

    return 0;
}


static const struct pinconf_ops local_virt_pconf_ops = {
    .pin_config_set		= local_virt_pconf_set,
    .pin_config_group_set = local_virt_pconf_group_set,
    .pin_config_get = local_virt_pconf_get,
    .pin_config_group_get = local_virt_pconf_group_get,
    .pin_config_dbg_show	= local_virt_pconf_dbg_show,
	.pin_config_group_dbg_show = local_virt_pconff_group_dbg_show,
};


int local_virt_pinctrl_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    struct device_node *np = dev->of_node;
    DEBUG_LOG("Enter!");

    if (!np) {
        DEBUG_LOG("Invalid device node!");
        return -1;
    }

    // 分配desc
    g_vpinctrl = devm_kzalloc(dev, sizeof(*g_vpinctrl), GFP_KERNEL);
    if (!g_vpinctrl) {
        DEBUG_LOG("Alloc pinctrl memory failed!");
        return -1;
    }

    // 设置desc
    // - 设置基本信息
    g_vpinctrl->dev = dev;
    g_vpinctrl->pctl_desc.name = dev_name(dev);
    g_vpinctrl->pctl_desc.owner = THIS_MODULE;
    // - 设置pins和npins
    g_vpinctrl->pctl_desc.npins = ARRAY_SIZE(my_pins_desc);
    g_vpinctrl->pctl_desc.pins = my_pins_desc;

    // 构造group表
    if (build_vptrl_group_tab(g_vpinctrl)) {
        DEBUG_LOG("Build pinctrl groups table failed!");
        return -1;
    }

    // 从设备树中获取配置的pin脚，复用功能和config


    // - 设置oprt结构体
    //      * pctlops
    g_vpinctrl->pctl_desc.pctlops = &local_virt_pctrl_ops;
    //      * pmxops
    g_vpinctrl->pctl_desc.pmxops = &local_virt_pmx_ops;
    //      * confops
    g_vpinctrl->pctl_desc.confops = &local_virt_pconf_ops;

    g_vpinctrl->pctl_desc.link_consumers = true;

    //注册desc
    g_vpinctrl->pin_dev = devm_pinctrl_register(dev, &g_vpinctrl->pctl_desc, g_vpinctrl);
    if (IS_ERR(g_vpinctrl->pin_dev)) {
        DEBUG_LOG("Register pincontroller failed!");
        return -1;
    }

    DEBUG_LOG("Probe end!");
    return 0;
}


int local_virt_pinctrl_remove(struct platform_device *pdev)
{
    DEBUG_LOG("Enter!");

    return 0;
}

static const struct of_device_id local_virt_pinctrl_of_match[] = {
    { .compatible = "st,100ask_virt_pinctrl" },
    {},
};

static struct platform_driver local_virt_pinctrl_driver = {
    .probe		= local_virt_pinctrl_probe,
    .remove		= local_virt_pinctrl_remove,
    .driver		= {
        .name	= "local_virt_pinctrl",
        .of_match_table = local_virt_pinctrl_of_match,
    },
};

module_platform_driver(local_virt_pinctrl_driver);
