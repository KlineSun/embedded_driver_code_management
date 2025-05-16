#ifndef BTN_GPIOS_H
#define BTN_GPIOS_H

#define MIN(x, y) x < y ? x : y
#define DEBUG_LOG(fmt, ...) \
        printk(KERN_INFO "[%s line%d] %s(): "fmt"\n", \
            KBUILD_BASENAME, __LINE__, __FUNCTION__, ##__VA_ARGS__)

#define GROUP_PIN(x, y) ((x << 8) | y)

#define KEY_DOWN  (1)
#define KEY_UP   (0)
#define MAX_DEV_NAME_LEN    (64)
#define MAX_GPIO_BUTN_NUM   (16)

struct button_operations {
    // 从dev_drv匹配后获取到的设备名称，将作为创建设备节点时的名称；
    char dev_name[MAX_DEV_NAME_LEN];

    // 初始化函数
    int (*init)(void);

    // 配置函数，使硬件处于就绪状态
    int (*config)(int btn_num);

    // 控制函数，对led进行操控
    int (*ctrl)(int btn_num, char data);

    // 检查led的状态
    int (*check)(int btn_num, char *status, int size);

    // 关闭文件时，清除节点文件独有资源
    int (*release)(int btn_num);

    // 释放所有的资源
    void (*destroy)(void);
};

int button_operations_register(struct button_operations *ops);
int button_operations_unregister(struct button_operations *ops);
int button_device_create(int minor);
int button_device_destroy(int minor);
#endif // BTN_GPIOS_H