#ifndef LED_DRV_H
#define LED_DRV_H

#define MIN(x, y) x < y ? x : y
#define DEBUG_LOG(fmt, ...) \
        printk(KERN_INFO "[%s line%d] %s(): "fmt"\n", \
            KBUILD_BASENAME, __LINE__, __FUNCTION__, ##__VA_ARGS__)

#define GROUP_PIN(x, y) ((x << 8) | y)

#define LED_ON  (1)
#define LED_OFF (0)
#define MAX_LED_DEV_NAME_LEN (64)
#define MAX_LED_NUM_SUPPORT (64)

struct led_operations {
    // 从dev_drv匹配后获取到的设备名称，将作为创建设备节点时的名称；
    char dev_name[MAX_LED_DEV_NAME_LEN];

    // 初始化函数
    int (*init)(void);

    // 配置函数，使硬件处于就绪状态
    int (*config)(int led_num);

    // 控制函数，对led进行操控
    int (*ctrl)(int led_num, char data);

    // 检查led的状态
    int (*check)(int led_num, char *status, int size);

    // 关闭文件时，清除节点文件独有资源
    int (*release)(int led_num);

    // 释放所有的资源
    void (*destroy)(void);
};

int led_operations_register(struct led_operations *ops);
int led_operations_unregister(struct led_operations *ops);
int led_device_create(int minor);
int led_device_destroy(int minor);
#endif // LED_DRV_H