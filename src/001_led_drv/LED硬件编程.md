# LED硬件编程

## 一、普适的GPIO引脚操作方法

### 1、GPIO模块一般结构

- 有多组GPIO，每组有多个GPIO；
- 使能：电源/时钟
- 模式：GPIO模式或者其他模式，因为一个引脚不只是可用于GPIO，还有其他用途；
- 方向：引脚Mode设置为GPIO时，可以设置它是输出引脚还是输入引脚；
- 数值：
  - 对于输出引脚，可以设置寄存器让其输出高、低电平；
  - 对于输入引脚，可以读取寄存器得到引脚的当前电平；

![image-20250425132418866](E:\embedded_learning\embedded_driver_code_management\src\001_led_drv\image-20250425132418866.png)





### 2、GPIO寄存器操作

#### 1.2.1 操作原则

不要影响到其他位，分以下步骤：

- Read：读取寄存器原来的值；
- Modify：修改寄存器的值(位运算)；
- Write：写回到寄存器中；





### 1.2.2 set_and_clear protocol

如果支持set_reg、clr_reg（称为set_and_clear protocol），则可以如此操作：

set_reg = 1;

// 给bit0写1，其他位不会变；

set_reg = 5;

// 给bit0、bit2写1，其他位不会变；

clr_reg = 1;

// 给bit0写0，其他位不会变；

clr_reg = 5;

// 给bit0、bit2写0，其他位不会变；



#### 1.2.3 STM32MP157的GPIO操作方法

- 依赖PLL4提供时钟信号

相较于其他芯片，在最开始多了一个步骤：使能PLL；

只能PLL之后，才能提供时钟信号；

以上信息通过查找CPU芯片手册(DM00327659.pdf)确认：

P522 RCC章节:

> The PLL4 is dedicated to the generation of the kernel clocks for various peripherals

SMT32MP157有两个核A7核和一个M4核，PLL属于A核侧；

- 使能PLL4，找到对应的寄存器（**RCC_PLL4CR**，位于P682），主要使用以下两位：
  ***基地址：0x50000000， 偏移地址：0x894***

  - Bit 0 **PLLON**: PLL4 enable，使能PLL4，1为使能，0为关闭；

  - Bit 1 **PLL4RDY**: PLL4 clock ready flag，用来标记PLL4是否就绪，1表示locked，0表示unlocked

    先将bit0设置为1来使能PLL4，再循环等待bit2为1，表示PLL4已就绪；

  其余位简单说明：

  - Bit 2 **SSCG_CTRL**: Clock Spreading Generator of PLL4 enable，**扩频时钟生成器**使能开关，用于降低峰值EMI，适用于对EMI敏感的应用（如高速显示接口、无线通信共存环境）；
  - Bit 4 **DIVPEN**: PLL4 DIVP divider output enable， **PLL4 DIVP分频器输出使能**
  - Bit 5 **DIVQEN**: PLL4 DIVQ divider output enable，**PLL4 DIVQ分频器输出使能**
  - Bit 6 **DIVREN**: PLL4 DIVR divider output enable，**PLL4 DIVR分频器输出使能**

  **DIVP、DIVQ、DIVR**是三个独立的**输出分频器**，用于将PLL4的核心频率（VCO输出）分频后生成不同的时钟信号，供不同外设使用，也就是一个VCO时钟源可以分成三个不同的时钟信号供外部使用；

- GPIO模块式是A7、M4公用的，如何确定是哪边来使用，需要通过寄存器来设置：
  ***基地址：0x50000000， 偏移地址：0xA28***

  操作**RCC_MP_AHB4ENSETR**寄存器，就由MPU来操作，也就是A7；

  操作**RCC_MC_AHB4ENSETR**寄存器，就由MCU来操作，也就是M4；
  他们的寄存器类似，都是使能某组GPIO（GPIOA~GPIOK）的时钟，比如使能A7侧GPIOA的时钟，可以设置**RCC_MP_AHB4ENSETR**寄存器的bit0：

  - Bit 0 **GPIOAEN**: GPIOA peripheral clocks enableSet by software.

    0: Writing '0' has no effect, reading '0' means that the peripheral clocks are disabled

    1: Writing '1' enables the peripheral clocks, reading '1' means that the peripheral clocks are 

    enabled

- 设置某组GPIO的模式，操作**GPIOx_MODER**(x = A to K, Z)寄存器：

  ***基地址：0x50002000，偏移地址：0x00***
  Bits 31:0 **MODER[15:0][1:0]:** Port x configuration I/O pin y (y = 15 to 0)

  These bits are written by software to configure the I/O mode.

  00: Input mode，输入模式

  01: General purpose output mode，一般输出模式

  10: Alternate function mode，替补功能模式

  11: Analog mode，模拟模式
  GPIO一般选择00/01

- 设置GPIO的输出类型，操作**GPIOx_OTYPER**(x = A to K, Z)寄存器：

  ***基地址：0x50002000，偏移地址：0x04***
  Bits 31:16 Reserved, must be kept at reset value.

  Bits 15:0 **OT[15:0]:** Port x configuration I/O pin y (y = 15 to 0)

  These bits are written by software to configure the I/O output type.

  0: Output push-pull (reset state)，推挽输出

  1: Output open-drain，开漏输出

- 设置GPIO 输出的速度，操作**GPIOx_OSPEEDR**寄存器：

  ***基地址：0x50002000，偏移地址：0x08***
  Bits 31:0 **OSPEEDR[15:0][1:0]**: Port x configuration I/O pin y (y = 15 to 0)

  These bits are written by software to configure the I/O output speed.

  00: Low speed

  01: Medium speed

  10: High speed

  11: Very high speed

  *Note: Refer to the product datasheets for the values of OSPEEDRy bits versus V**DD* *range* 

  *and external load.*

- 设置GPIO上下拉电阻，操作**GPIOx_PUPDR**寄存器：

  ***基地址：0x50002000，偏移地址：0x0C***
  Bits 31:0 **PUPDR[15:0][1:0]:** Port x configuration I/O pin y (y = 15 to 0)

  These bits are written by software to configure the I/O pull-up or pull-down

  00: No pull-up, pull-down

  01: Pull-up

  10: Pull-down

  11: Reserved

- 读取GPIO引脚电平，操作**GPIOx_IDR**寄存器：
  ***基地址：0x50002000，偏移地址：0x10***

  Bits 31:16 Reserved, must be kept at reset value.

  Bits 15:0 **IDR[15:0]:** Port x input data I/O pin y (y = 15 to 0)

  These bits are read-only. They contain the input value of the corresponding I/O port.

- GPIO输出电平，操作**GPIOx_ODR**寄存器来实现引脚电平的操控：

  ***基地址：0x50002000，偏移地址：0x14***
  Bits 31:16 Reserved, must be kept at reset value.

  Bits 15:0 **ODR[15:0]:** Port output data I/O pin y (y = 15 to 0)

  These bits can be read and written by software.

  *Note: For atomic bit set/reset, the ODR bits can be individually set and/or reset by writing to* 

  *the GPIOx_BSRR or GPIOx_BRR registers (x = A..F*

- 快速设置GPIO的某一位，可以操作**GPIOx_BSRR**寄存器：
  ***基地址：0x50002000，偏移地址：0x18***

  ![image-20250425171034332](C:\Users\Jianyuan Sun\AppData\Roaming\Typora\typora-user-images\image-20250425171034332.png)
  Bits 31:16 **BR[15:0]:** Port x reset I/O pin y (y = 15 to 0)

  These bits are write-only. A read to these bits returns the value 0x0000.

  0: No action on the corresponding ODRx bit

  1: Resets the corresponding ODRx bit

  *Note: If both BSx and BRx are set, BSx has priority.*

  Bits 15:0 **BS[15:0]:** Port x set I/O pin y (y = 15 to 0)

  These bits are write-only. A read to these bits returns the value 0x0000.

  0: No action on the corresponding ODRx bit

  1: Sets the corresponding ODRx bit





## 二、查看原理图确认LED控制引脚；

### 1、查看丝印图

在丝印图上确认需要操作的LED是哪一个元件名称，比如在主板左上角的LED2：

![image-20250425171335503](C:\Users\Jianyuan Sun\AppData\Roaming\Typora\typora-user-images\image-20250425171335503.png)



### 2、查看原理图

在原理图中，找到丝印图中锁定的元件名，如LED2：

![image-20250425171515477](C:\Users\Jianyuan Sun\AppData\Roaming\Typora\typora-user-images\image-20250425171515477.png)

从上述原理图可以知道：

- LED2连接到PA10这个GPIO口上；
- LED2是一个绿色的LED灯；
- **PA10输出高电平时，LED2无电流流过，灯灭；**
- **PA10输出低电平时，LED2有电流流过，灯亮；**





### 3、编写驱动程序

#### 2.3.1 驱动模板编写步骤

按照之前提到的套路：

- 先确定一个主设备号，方便内核找到该驱动；
- 为驱动定制file_operations结构体；
- 将定制好的file_ops告诉内核，通过调用register_chrdev(major, drv_name, file_ops);如果不需要指定major主设备号，可以传入0，由内核自动分配，他会在255~0这个区间内查找，第一个空编号就作为当前驱动的主设备号；
- 补全驱动的入口函数和出口函数；可以在入口函数中创建class(class_create)，让内核给我们创建一个dev节点(device_create);在出口函数中，需要对当前驱动所申请的一些资源进行释放，比如释放注册的设备，释放创建的类和设备节点，按照与创建顺序相反的顺序来进行释放，比如：device_destroy -> class_destroy -> unregister_chrdev
- 把入口函数、出口函数告诉内核，module_init、module_exit；
- 最后，完善GPL证书、作者、模块描述等信息；



#### 2.3.2 STM32 GPIO驱动硬件编程步骤

- 驱动程序不能直接访问到硬件寄存器的物理地址，必须先将物理地址通过ioremap映射到一个虚拟地址，之后通过虚拟地址来访问该寄存器；
- 查询模块的基地址，可以在芯片手册P158查看，**Table 9. Register boundary addresses**，找到需要操作的模块的基地址是多少，然后加上寄存器的offset，就是当前寄存器的物理地址：
  ![image-20250425180159516](C:\Users\Jianyuan Sun\AppData\Roaming\Typora\typora-user-images\image-20250425180159516.png)
  例如RCC的基地址是0x50000000，RCC_PLL4CR寄存器的offset是0x894，公式如下：
  phy_addr = base_addr + offset;
  RCC_PLL4CR的物理地址 = 0x50000000 + 0x894;

- 在入口函数中进行物理地址的映射，在出口函数中反映射；
- 在open函数中，做一下操作：
  - 使能PLL4：将**RCC_PLL4CR**寄存器的bit0置1，并等待bit1的值为1；
  - 设置RCC对A7核生效：将**RCC_MP_AHB4ENSETR**寄存器的bit0置1，使能GPIOA的时钟；
- 在写函数中对**GPIOx_ODR**寄存器进行写操作，两种GPIO设置方法都实现；
  - 检查是否处于输出模式
    - 设置GPIOA Pin10为输出模式：先清除**GPIOx_MODER**寄存器20、21两位的值；然后写入01；
  - PA10输出电平：往**GPIOx_ODR**寄存器的bit10写入0，点亮LED，写入1，熄灭LED；
  - set_clear_protocol设置电平：**GPIOx_BSRR**寄存器的赋值(1 << 26)点亮LED，赋值(1<<10)，熄灭LED；
- 在读函数中读取**GPIOx_IDR**寄存器的值，返回给APP；
  - 检查是否处于输入模式，不是则进入输入模式
    - 设置GPIOA Pin10为输出模式：先清除**GPIOx_MODER**集群起20、21两位的值；然后写入00；
  - 读取**GPIOx_IDR**寄存器的值，位运算获取pin脚状态；



