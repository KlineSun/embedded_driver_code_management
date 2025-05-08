#ifndef LED_DRV_H
#define LED_DRV_H

#define MIN(x, y) x < y ? x : y
#define DEBUG_LOG(fmt, ...) \
        printk(KERN_INFO "[%s line%d] %s(): "fmt"\n", \
            KBUILD_BASENAME, __LINE__, __FUNCTION__, ##__VA_ARGS__)

#define LED_ON  (1)
#define LED_OFF (0)

struct led_operations {
    int num;
    // 初始化函数
    int (*init)(void);

    // 配置函数，使硬件处于就绪状态
    int (*config)(int led_num);

    // 控制函数，对led进行操控
    int (*ctrl)(int led_num, char data);

    // 检查led的状态
    int (*check)(int led_num, char *status, int size);

    // 释放所有的资源
    void (*destroy)(void);
};

struct led_operations *get_led_oprts(void);

#endif // LED_DRV_H