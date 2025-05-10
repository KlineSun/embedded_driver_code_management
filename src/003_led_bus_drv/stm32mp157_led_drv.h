#ifndef STM32MP157_LED_CONFIG_H
#define STM32MP157_LED_CONFIG_H

// register offset defines
#define RCC_REG_ADDR        (0x50000000)
#define GPIO_REG_BASE_ADDR      (0x50002000)
#define GPIO_GROUP_REG_SIZE     (0x1000)
#define GPIO_GROUP_REGS_BASE_ADDR(group) (GPIO_REG_BASE_ADDR + (group * GPIO_GROUP_REG_SIZE))

#define MAX_GPIO_GROUP_NUM (11)
#define MAX_GPIO_PIN_NUM (16)

#define GPIO_INPUT_MODE     (0b00)
#define GPIO_OUTPUT_MODE    (0b01)
#define GPIO_ALT_MODE       (0b10)
#define GPIO_ANALOG_MODE    (0b11)

typedef struct {
    volatile unsigned int MODER;
    volatile unsigned int OTYPER;
    volatile unsigned int OSPEEDR;
    volatile unsigned int PUPDR;
    volatile unsigned int IDR;
    volatile unsigned int ODR;
    volatile unsigned int BSRR;
    volatile unsigned int LCKR;
    volatile unsigned int AFR[2];
} stm32mp157_gpio_regs;

#endif // STM32MP157_LED_CONFIG_H