# 总线设备驱动模型

## 一、驱动设计思想

### 1、面向对象

- 对于字符设备驱动，我们抽象出了一个file_operations结构体；
- 对于硬件部分，例如LED，我们抽象出了一个led_operations结构体，用来表示一个LED的操作接口；



### 2、分层

上下分层，将驱动程序分为两层，比如：

![image-20250509145434508](E:\embedded_learning\embedded_driver_code_management\src\003_led_bus_drv\image-20250509145434508.png)



### 3、分离

我们的驱动操作，如board_A.c中，有芯片相关的操作，也有主板相关的操作，因此，可以将芯片通用的代码分离出来，用来作为通用的接口，将硬件连接相关的代码也单独作为一个代码文件，比如led，如下：

![image-20250509150836097](E:\embedded_learning\embedded_driver_code_management\src\003_led_bus_drv\image-20250509150836097.png)

以面向对象的思想，在 board_A_led.c 中实现 led_resouce 结构体，它定义“资源”──要用哪一个引脚。

在 chipY_gpio.c 中仍是实现 led_operations 结构体，它要写得更完善，支持所有 GPIO。



### 4、总线设备驱动模型

前面提到的三种思想都存在一定的弊端，即使使用到分离的设计思想， 依旧会有问题，比如不同的主板上，LED连接的pin脚可能不同，因此就需要单独为之编写一个board_A_led.c文件，用来配置LED的信息；这样下来，会导致内核中存在大量冗余的硬件配置代码，而这些代码往往是不通用的，因为不同的板子接线不同；

为了解决以上问题，引入了总线设备驱动，我们使用platform_device结构体来表示连接在芯片上的各个设备，结构体原型如下:

```c
struct platform_device {
	const char	*name; //设备名称
	int		id; //设备实例ID
	bool		id_auto; //指示是否自动分配ID的标志。如果为true，内核会自动管理设备ID
	struct device	dev; //内嵌的标准设备结构体，包含设备模型核心信息(如父设备、电源管理、sysfs条目等)
	u64		platform_dma_mask; //设备的DMA掩码，指定设备能够寻址的DMA地址范围
	u32		num_resources; //设备资源(如I/O内存、中断等)的数量
	struct resource	*resource; //指向设备资源数组的指针，描述设备的硬件资源(如寄存器地址范围、中断号等)

	const struct platform_device_id	*id_entry; //指向平台设备ID表的指针，用于与驱动匹配
    // 强制指定要绑定的驱动名称，覆盖正常的匹配过程
	char *driver_override; /* Driver name to force a match */

	/* MFD cell pointer */
	struct mfd_cell *mfd_cell; //如果设备是多功能设备(MFD)的一部分，指向MFD单元信息

	/* arch specific additions */
	struct pdev_archdata	archdata; //架构特定的扩展数据，不同架构可能有不同的内容
};

struct platform_driver {
    // 当驱动与设备匹配成功后调用的探测函数。负责初始化设备、分配资源、注册设备等操作。
	int (*probe)(struct platform_device *);
    // 当设备被移除或驱动卸载时调用的清理函数。负责释放资源、注销设备等操作。
	int (*remove)(struct platform_device *);
    // 系统关机时调用的函数，用于安全地关闭设备。
	void (*shutdown)(struct platform_device *);
    // 设备挂起时调用的函数，用于保存设备状态并进入低功耗模式。
	int (*suspend)(struct platform_device *, pm_message_t state);
    // 设备从挂起状态恢复时调用的函数。
	int (*resume)(struct platform_device *);
    /*
     内嵌的标准驱动结构体，包含：
        name: 驱动名称，用于与platform_device匹配
        owner: 通常设为THIS_MODULE
        of_match_table: 指向设备树兼容性匹配表的指针
        pm: 电源管理操作集
    */
	struct device_driver driver;
    // 声明自己支持哪些设备，指向平台设备ID表的指针，用于非设备树情况下的驱动与设备匹配
	const struct platform_device_id *id_table;
    // 如果设为true，当探测失败时不会延迟重试(即禁用 deferred probe 机制)
	bool prevent_deferred_probe;
};

// 其中device_driver结构体如下：
struct device_driver {
    // 驱动程序的名称，用于与设备platform_device匹配的关键标识
	const char		*name;
    // 指向该驱动所属总线类型的指针（如 &platform_bus_type）
	struct bus_type		*bus;

    // 指向拥有该驱动的模块（通常设为 THIS_MODULE）
	struct module		*owner;
    // 用于内置模块的模块名称
	const char		*mod_name;	/* used for built-in modules */
	// 设为 true 时，禁止通过 sysfs 进行 bind/unbind 操作
	bool suppress_bind_attrs;	/* disables bind/unbind via sysfs */
    /*
    指定探测类型，可以是：
        PROBE_DEFAULT_STRATEGY
        PROBE_PREFER_ASYNCHRONOUS
        PROBE_FORCE_SYNCHRONOUS
    */
	enum probe_type probe_type;
	// 设备树匹配表，用于与设备树节点兼容性字符串匹配
	const struct of_device_id	*of_match_table;
    // ACPI 匹配表，用于与 ACPI 设备 ID 匹配
	const struct acpi_device_id	*acpi_match_table;
	// 设备探测函数，当驱动与设备匹配时调用
	int (*probe) (struct device *dev);
    // 设备移除函数，当设备断开或驱动卸载时调用
	int (*remove) (struct device *dev);
    // 系统关机时调用的设备关闭函数
	void (*shutdown) (struct device *dev);
    // 设备挂起函数，进入低功耗状态时调用
	int (*suspend) (struct device *dev, pm_message_t state);
    // 设备恢复函数，从低功耗状态唤醒时调用
	int (*resume) (struct device *dev);
    // 驱动默认属性组，在 sysfs 中创建对应的属性文件
	const struct attribute_group **groups;
    // 设备默认属性组，为每个关联设备创建 sysfs 属性文件
	const struct attribute_group **dev_groups;
	// 指向电源管理操作集的指针
	const struct dev_pm_ops *pm;
    // 设备核心转储函数，用于收集设备调试信息
	void (*coredump) (struct device *dev);
	// 驱动私有数据，由设备模型核心使用
	struct driver_private *p;
};
```

初始化platform_device和platform_drive结构体的示例如下：

![image-20250509152238479](E:\embedded_learning\embedded_driver_code_management\src\003_led_bus_drv\image-20250509152238479.png)



注册platform_device(使用platform_device_register)和注册platform_driver(使用platform_driver_register)时，会把对应的pdev和pdrv保存在platform_bus_type总线的两个列表中，总线的结构体是：

```c
struct bus_type {
	const char		*name;
	const char		*dev_name;
	struct device		*dev_root;
	const struct attribute_group **bus_groups;
	const struct attribute_group **dev_groups;
	const struct attribute_group **drv_groups;

	int (*match)(struct device *dev, struct device_driver *drv);
	int (*uevent)(struct device *dev, struct kobj_uevent_env *env);
	int (*probe)(struct device *dev);
	int (*remove)(struct device *dev);
	void (*shutdown)(struct device *dev);

	int (*online)(struct device *dev);
	int (*offline)(struct device *dev);

	int (*suspend)(struct device *dev, pm_message_t state);
	int (*resume)(struct device *dev);

	int (*num_vf)(struct device *dev);

	int (*dma_configure)(struct device *dev);

	const struct dev_pm_ops *pm;

	const struct iommu_ops *iommu_ops;

	struct subsys_private *p;
	struct lock_class_key lock_key;

	bool need_parent_lock;
};

struct bus_type platform_bus_type = {
	.name		= "platform",
	.dev_groups	= platform_dev_groups,
	.match		= platform_match,
	.uevent		= platform_uevent,
	.dma_configure	= platform_dma_configure,
	.pm		= &platform_dev_pm_ops,
};

```

platform_device和platform_driver匹配机制：

1. 先检查struct platform_device结构体的driver_override字段，是否有指定需要强制匹配哪个驱动，如果有，则去寻找指定的驱动名称；
2. 若未指定driver_override字段，接着比较platform_device的name和platform_driver的id_table字段是否有匹配项（id_table表示platform_driver支持的设备有哪些）；
3. 以上均为匹配成功，则比较platform_device和platform_driver的name是否一致；

platform_match匹配函数的代码如下：

```c
static int platform_match(struct device *dev, struct device_driver *drv)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct platform_driver *pdrv = to_platform_driver(drv);

	/* When driver_override is set, only bind to the matching driver */
    // 第一种匹配
	if (pdev->driver_override)
		return !strcmp(pdev->driver_override, drv->name);

	/* Attempt an OF style match first */
    // 设备树相关，先跳过
	if (of_driver_match_device(dev, drv))
		return 1;

	/* Then try ACPI style match */
	if (acpi_driver_match_device(dev, drv))
		return 1;

	/* Then try to match against the id table */
    //第二种匹配
	if (pdrv->id_table)
		return platform_match_id(pdrv->id_table, pdev) != NULL;

	/* fall-back to driver name match */
    //第三种匹配
	return (strcmp(pdev->name, drv->name) == 0);
}

```



注册调用顺序：

```
platform_device_register
platform_device_add
 device_add
 bus_add_device // 放入链表
 bus_probe_device // probe 枚举设备，即找到匹配的(dev, drv)
 device_initial_probe
 __device_attach
 bus_for_each_drv(...,__device_attach_driver,...)
 __device_attach_driver
 driver_match_device(drv, dev) // 是否匹配
 driver_probe_device // 调用 drv 的 probe

platform_driver_register
__platform_driver_register
 driver_register
 bus_add_driver // 放入链表
 driver_attach(drv)
 bus_for_each_dev(drv->bus, NULL, drv, __driver_attach);
 __driver_attach
 driver_match_device(drv, dev) // 是否匹配
 driver_probe_device // 调用 drv 的 probe
```





## 二、怎么写程序

- 分配、设置、注册platform_device 结构体；
- 分配、设置、注册platform_driver 结构体；
- 在platform_driver 结构体的probe函数中，分配、设置、注册file_operations结构体；
- 在platform_device中，指定使用的硬件资源，指定对应platform_driver的名字；



### 1、板级代码

编写顺序(用于提供platform_device)：

- 定义一个platform_device结构体，初始化name、id、num_resources、resource、.dev.release等成员，其中，**release函数不可缺少，否则会发生崩溃**，详细将调试总结；
- 编写初始函数和退出函数，分别注册和反注册该结构体；
- 定义想要操作的硬件资源，构造struct resource结构体，指明start(作为probe函数执行时的参数)、flags(资源类型)等参数；
- 定义好资源数组之后，将资源填充到platform_device结构体的resource、num_resource中；



### 2、芯片级代码

编写顺序(用于提供驱动)：

- 定义一个platform_driver结构体，初始化.driver.name、probe、remove等基础信息；
- 编写初始函数和退出函数，分别注册和反注册该结构体；
  注：内核其他代码中使用module_platform_driver来注册，**该部分的差异待完善**；
- 编写probe函数，在platform_driver和platform_device匹配之后执行，通常用来记录platform_device中存在的硬件资源，可以将硬件资源转化成驱动中的对象，比如调用device_create将led转化成多个dev节点，方便分开控制；
- 编写remove函数，在设备被反注册时执行，通常用来完成硬件资源的释放，比如device_destroy；



### 3、驱动级代码

编写顺序(用来提供驱动常用的接口，也就是驱动节点的file_operations结构体)：

- 定义file_operations结构体；
- 实现各个成员函数；
- 定义入口函数和出口函数；
- 在入口函数中完成字符设备的注册、class注册
- 在出口函数中完成资源的销毁；
- 由于硬件资源的数量和类型都不确定，因此提供创建设备节点的接口，供芯片级驱动根据获得的硬件资源来调用；
- 提供注册类似led_operations结构体的接口，**构建 *芯片级代码* 依赖 *驱动级代码* 的关系**，避免出现两者交叉依赖；



## 三、调试总结

- 在当前内核版本中，注册platform_device时，必须要指定release函数，如下：

  ```c
  static void board_100ask_led_release(struct device *dev)
  {
      // 这里可以释放设备占用的私有资源（如果有）
      DEBUG_LOG("100ask board released");
  }
  
  static struct platform_device board_100ask_led_pdev = {
      .name = "100ask_led",
      .id = 0,
      .num_resources = ARRAY_SIZE(board_100ask_led_res),
      .resource = board_100ask_led_res,
      .dev = {
          .release = board_100ask_led_release, // 关键修复！
      },
  };
  ```

  若不指定，会发生崩溃，如下：

  >[ 5103.908071] ------------[ cut here ]------------ [ 5103.913670] WARNING: CPU: 1 PID: 19087 at drivers/base/core.c:1104 device_release+0x94/0x98 [ 5103.920961] Device '100ask_led.0' does not have a release() function, it is broken and must be fixed. See Documentation/kobject.txt. [ 5103.932942] Modules linked in: stm32mp157_led_drv(OE) 100ask_board_led_res(OE-) led_base_drv(OE) aes_arm_bs crypto_simd usbip_host cryptd algif_skcipher usbip_core galcore(O) hci_uart btqca bcmdhd btbcm btintel stm32_cec sch_fq_codel ipv6 nf_defrag_ipv6 [last unloaded: stm32mp157_led_drv] [ 5103.958522] CPU: 1 PID: 19087 Comm: rmmod Tainted: G           OE     5.4.31 #1 [ 5103.965681] Hardware name: STM32 (Device Tree Support) [ 5103.970861] [<c01123e4>] (unwind_backtrace) from [<c010d6c8>] (show_stack+0x10/0x14)
  >
  >...











