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
#include <linux/of_device.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <linux/slab.h>
#include "i2c_adapter_drv.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kline <kline_code@example,com>");
MODULE_DESCRIPTION("Test for virtual i2c adapter");

#define VIRTUAL_EEPROM_SLAVE_ADDR (0x50)
#define VIRTUAL_EEPROM_SIZE (1024)

static struct i2c_adapter *g_i2c_adpt = NULL;
static char *eeprom_buf = NULL;
static int eeprom_cur_addr = 0;

static int eeprom_enumlator_transfer(struct i2c_adapter *adap, struct i2c_msg *msg)
{
    int transfer_cnt = 0, i = 0;
    DEBUG_LOG("Enter!");
    if (msg->flags & I2C_M_RD) {
        DEBUG_LOG("Read data!");
        for (i = 0; i < msg->len; i++) {
            msg->buf[i] = eeprom_buf[eeprom_cur_addr++];
            if (eeprom_cur_addr == VIRTUAL_EEPROM_SIZE)
                eeprom_cur_addr = 0; // reset to 0

            transfer_cnt++;
        }
    } else {
        DEBUG_LOG("Write data, length: %d", msg->len);
        if (msg->len == 1) {
            // set current address if len equal to 1
            eeprom_cur_addr = msg->buf[0];
            transfer_cnt++;
        } else if (msg->len > 1) {
            // block write
            eeprom_cur_addr = msg->buf[0];
            transfer_cnt++;
            for (i = 1; i < msg->len; i++) {
                eeprom_buf[eeprom_cur_addr++] = msg->buf[i];
                if (eeprom_cur_addr == VIRTUAL_EEPROM_SIZE)
                    eeprom_cur_addr = 0; // reset to 0
                transfer_cnt++;
            }
        } else {
            DEBUG_LOG("Invalid msg length!");
        }
    }
    return transfer_cnt;
}

static int local_virt_i2c_master_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
    int i = 0, ret = 0, total = 0;

    if (!g_i2c_adpt || !eeprom_buf) {
        DEBUG_LOG("Exceptional initialization!");
        return -1;
    }
    DEBUG_LOG("Enter!");


    for (i = 0; i < num; i++) {
        if (msgs[i].addr == VIRTUAL_EEPROM_SLAVE_ADDR) {
            ret = eeprom_enumlator_transfer(adap, &msgs[i]);
            if (ret != msgs[i].len) {
                DEBUG_LOG("Comparison of transfer length failed: expect_len=%d, actual_len=%d",
                            msgs[i].len, ret);
                return -1;
            }
            total += ret;
        } else {
            DEBUG_LOG("Unsupport slave address: %02x", msgs[i].addr);
            continue;
        }
    }
    return total <= 0 ? -1 : i;
}

static u32 local_virt_i2c_functionality(struct i2c_adapter *adap)
{
    DEBUG_LOG("Enter!");
    return I2C_FUNC_I2C |
        I2C_FUNC_SMBUS_BYTE |
        I2C_FUNC_SMBUS_BYTE_DATA |
        I2C_FUNC_SMBUS_WORD_DATA |
        I2C_FUNC_SMBUS_BLOCK_DATA |
        I2C_FUNC_SMBUS_I2C_BLOCK |
        I2C_FUNC_SMBUS_PROC_CALL |
        I2C_FUNC_SMBUS_BLOCK_PROC_CALL;
}

static struct i2c_algorithm local_virt_i2c_algo = {
    .master_xfer = local_virt_i2c_master_xfer,
    .functionality = local_virt_i2c_functionality,
};

int local_i2c_adapter_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    struct device_node *np = dev->of_node;
    int adapter_nr = -1;
    DEBUG_LOG("Enter!");

    if (!np) {
        DEBUG_LOG("Invalid device node!");
        return -1;
    }

    if (of_property_read_u32(np, "adpt_nr", &adapter_nr) < 0) {
        DEBUG_LOG("Read property from device node failed!");
        return -1;
    }
    DEBUG_LOG("Get i2c adapter number from dts: %d", adapter_nr);

    // alloc adapter
    g_i2c_adpt = devm_kzalloc(dev, sizeof(*g_i2c_adpt), GFP_KERNEL);
    if (!g_i2c_adpt) {
        DEBUG_LOG("Read property from device node failed!");
        return -ENOMEM;
    }

    // set adapter
    snprintf(g_i2c_adpt->name, sizeof(g_i2c_adpt->name), "%s%d", np->name, adapter_nr);
    g_i2c_adpt->nr = adapter_nr;
    g_i2c_adpt->owner = THIS_MODULE;
    g_i2c_adpt->algo = &local_virt_i2c_algo;

    
    // register adapter
    if (i2c_add_numbered_adapter(g_i2c_adpt) < 0) {
        DEBUG_LOG("Register i2c adapter%d failed!", adapter_nr);
        return -1;
    }

    // alloc eeprom buf
    eeprom_buf = kzalloc(VIRTUAL_EEPROM_SIZE, GFP_KERNEL);
    if (!eeprom_buf) {
        DEBUG_LOG("Alloc EEPROM buffer failed!");
        return -ENOMEM;
    }

    DEBUG_LOG("Probe end!");
    return 0;
}


int local_i2c_adapter_remove(struct platform_device *pdev)
{
    DEBUG_LOG("Enter!");
    if (eeprom_buf)
        kfree(eeprom_buf);

    i2c_del_adapter(g_i2c_adpt);
    return 0;
}

static const struct of_device_id local_i2c_adapter_of_match[] = {
    { .compatible = "100ask,local_i2c_adapter" },
    {},
};

static struct platform_driver local_i2c_adapter_driver = {
    .probe		= local_i2c_adapter_probe,
    .remove		= local_i2c_adapter_remove,
    .driver		= {
        .name	= "local_i2c_adapter",
        .of_match_table = local_i2c_adapter_of_match,
    },
};

module_platform_driver(local_i2c_adapter_driver);
