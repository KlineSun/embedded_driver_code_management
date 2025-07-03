#ifndef VIRTUAL_I2C_DRV_H
#define VIRTUAL_I2C_DRV_H

#define DEBUG_LOG(fmt, ...) \
        printk(KERN_INFO "[%s line%d] %s(): "fmt"\n", \
            KBUILD_BASENAME, __LINE__, __FUNCTION__, ##__VA_ARGS__)

#define MIN(x, y) x < y ? x : y


#define MAX_LCD_NAME_LEN (16)
#define local_set_bit(addr, bit)    writel(readl(addr) | BIT(bit), addr)
#define local_clear_bit(addr, bit)  writel(readl(addr) & ~BIT(bit), addr)

#endif // VIRTUAL_I2C_DRV_H