# LED驱动程序框架

## 一、驱动程序编程套路

按照之前提到的套路：

- 先确定一个主设备号，方便内核找到该驱动；
- 为驱动定制file_operations结构体；
- 将定制好的file_ops告诉内核，通过调用register_chrdev(major, drv_name, file_ops);如果不需要指定major主设备号，可以传入0，由内核自动分配，他会在255~0这个区间内查找，第一个空编号就作为当前驱动的主设备号；
- 补全驱动的入口函数和出口函数；可以在入口函数中创建class(class_create)，让内核给我们创建一个dev节点(device_create);在出口函数中，需要对当前驱动所申请的一些资源进行释放，比如释放注册的设备，释放创建的类和设备节点，按照与创建顺序相反的顺序来进行释放，比如：device_destroy -> class_destroy -> unregister_chrdev
- 把入口函数、出口函数告诉内核，module_init、module_exit；
- 最后，完善GPL证书、作者、模块描述等信息；





## 二、支持多板多灯的驱动框架

![led驱动框架](E:\embedded_learning\embedded_driver_code_management\src\002_led_frame_drv\led驱动框架.jpg)

框架如上；

设计思路：

1. 优先考虑硬件层，需要支持多板/多灯；

2. 不同的板/灯的寄存器/pin脚/操作时序灯均可能存在差异，对于有差异的部分要分开设计；

3. 每个板添加对应的文件来进行针对该板的寄存器操作，如board_a_led_coinfig.c，在该文件中实现boardA的寄存器操作和pin脚配置；
4. 将硬件操作抽象成四个动作：
   - init：完成一些必要的准备工作，比如寄存器的ioremap等，原则是不改变硬件状态，只为操作硬件做前期准备；
   - config：执行时机是驱动已经被打开，用户准备使用硬件，此时需要将硬件配置成就绪状态，方便快速响应用户的操作；
   - control：控制阶段，提供接口给驱动，方便得知用户意图之后，快速控制硬件响应控制需求；
   - check：检查阶段，如果用户只是想要查询硬件状态，需单独提供一个接口以便快速给出结果；
   - destroy：在推出驱动时，释放资源；
5. 以上四个动作抽象成一个对象，led_operations结构体，在驱动程序入口函数时注册给驱动程序；
6. 每个主板都按照以上步骤实现自己的led_operations结构体，在编译时，根据配置来选取不同的文件进行编译，从而注册到不同的led_operations结构体，用于操作对应的主板；
7. 在驱动函数中，根据时机执行led_operations提供的四个方法，如下：
   - 入口函数：此时驱动处于准备状态，硬件层一般不会被改变，因此需要执行init函数；
   - open函数：此时驱动已经被用户选中，随时会被使用，因此需要配置硬件为就绪状态，随时响应操作，所以要调用config函数；
   - write函数：此时用户需要操作硬件，下发指令，因此需要调用控制接口ctrl函数，根据用户下发的指令来控制硬件为不同的状态；
   - read函数：此时用户通常需要获取硬件状态，所以需要调用check函数，用来上报硬件当前的状态；
   - destroy函数：此时用户已经退出驱动，也就是调用了出口函数，在板代码上需要释放资源；



## 三、内核编译规则简介

### 1、选取多个源文件编译成一个ko

参考内核中drivers\char\ipmi\Makefile文件的代码：

```makefile
# 定义需要包含哪些.o文件，也就是需要编译哪些.c文件，格式为xxx-y
ipmi_si-y := ipmi_si_intf.o ipmi_kcs_sm.o ipmi_smic_sm.o ipmi_bt_sm.o \
	ipmi_si_hotmod.o ipmi_si_hardcode.o ipmi_si_platform.o \
	ipmi_si_port_io.o ipmi_si_mem_io.o

# 可以按照需要/条件添加特定的.o文件，用于编译特定的.c文件
ifdef CONFIG_PCI
ipmi_si-y += ipmi_si_pci.o
endif
ifdef CONFIG_PARISC
ipmi_si-y += ipmi_si_parisc.o
endif

# 根据需要，定义不同的配置下，需要被编译出来的模块
# obj-y会被编译进内核，obj-m会将包含的文件编译成.ko文件，如果编译宏没有被定义，则obj-不会生效，跳过编译
obj-$(CONFIG_IPMI_HANDLER) += ipmi_msghandler.o
obj-$(CONFIG_IPMI_DEVICE_INTERFACE) += ipmi_devintf.o
obj-$(CONFIG_IPMI_SI) += ipmi_si.o
obj-$(CONFIG_IPMI_DMI_DECODE) += ipmi_dmi.o
```

规则总结：

```makefile
# 定义需要哪些目标文件
obj_files-y := a.o b.o

# 可根据条件添加目标文件
ifdef condition1
obj_files-y += c.o
endif

# 编译输出结果可选以下几种：
# 将目标文件编译成.ko文件
obj-m += obj_files.o
# 将目标文件编译进内核
obj-y += obj_files.o
# 可根据BUILD_CONFIG1决定如何编译目标文件
obj-BUILD_CONFIG1 += obj_files.o

```











