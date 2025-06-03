#ifndef MMAP_TST_DRV_H
#define MMAP_TST_DRV_H

#define DEBUG_LOG(fmt, ...) \
        printk(KERN_INFO "[%s line%d] %s(): "fmt"\n", \
            KBUILD_BASENAME, __LINE__, __FUNCTION__, ##__VA_ARGS__)

#define MIN(x, y) x < y ? x : y

#endif // MMAP_TST_DRV_H