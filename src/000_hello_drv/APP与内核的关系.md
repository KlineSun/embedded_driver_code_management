# APP与内核的关系

## 一、文件在内核中的具体表现

### struct file结构体

我们的App打开一个文件时，在内核中会有一个struct file结构体与App得到的文件句柄相对应：

```c
int open(const char *pathname, int flags, mode_t mode);
```



内核include\linux\fs.h：

```c
struct file {
	union {
		struct llist_node	fu_llist;
		struct rcu_head 	fu_rcuhead;
	} f_u;
	struct path		f_path;
	struct inode		*f_inode;	/* cached value */
	const struct file_operations	*f_op;

	/*
	 * Protects f_ep_links, f_flags.
	 * Must not be taken from IRQ context.
	 */
	spinlock_t		f_lock;
	enum rw_hint		f_write_hint;
	atomic_long_t		f_count;
	unsigned int 		f_flags;
	fmode_t			f_mode;
	struct mutex		f_pos_lock;
	loff_t			f_pos;
	struct fown_struct	f_owner;
	const struct cred	*f_cred;
	struct file_ra_state	f_ra;

	u64			f_version;
#ifdef CONFIG_SECURITY
	void			*f_security;
#endif
	/* needed for tty driver, and maybe others */
	void			*private_data;

#ifdef CONFIG_EPOLL
	/* Used by fs/eventpoll.c to link all the hooks to this file */
	struct list_head	f_ep_links;
	struct list_head	f_tfile_llink;
#endif /* #ifdef CONFIG_EPOLL */
	struct address_space	*f_mapping;
	errseq_t		f_wb_err;
} __randomize_layout
  __attribute__((aligned(4)));	/* lest something weird decides that 2 is OK */

struct file_handle {
	__u32 handle_bytes;
	int handle_type;
	/* file identifier */
	unsigned char f_handle[0];
};
```

以上open函数传入的flags和mode参数，会被记录在f_flags、f_mode中；



### 1、基础字段

| 字段      | 类型                      | 说明                                          |
| :-------- | :------------------------ | :-------------------------------------------- |
| `f_count` | `atomic_long_t`           | 引用计数，跟踪有多少指针引用此 `file` 结构    |
| `f_flags` | `unsigned int`            | 文件打开标志（如 `O_RDONLY`、`O_NONBLOCK`）   |
| `f_mode`  | `fmode_t`                 | 文件访问模式（`FMODE_READ`、`FMODE_WRITE`）   |
| `f_pos`   | `loff_t`                  | 当前读写位置（偏移量），`llseek` 会修改它     |
| `f_inode` | `struct inode*`           | 指向文件的 `inode` 结构（实际文件数据）       |
| `f_op`    | `struct file_operations*` | 文件操作函数表（如 `read`、`write`、`ioctl`） |



### 2、文件操作

| 字段        | 说明                                            |
| :---------- | :---------------------------------------------- |
| `f_path`    | 文件路径结构（包含 `dentry` 和 `vfsmount`）     |
| `f_mapping` | 指向地址空间（`address_space`），用于页缓存管理 |
| `f_ra`      | 文件预读状态（readahead 优化）                  |
| `f_owner`   | 异步 I/O 通知的目标进程（如 `SIGIO`）           |
| `f_cred`    | 打开该文件的进程的凭据（`struct cred`）         |



### 3、高级功能字段

| 字段           | 用途                                               |
| :------------- | :------------------------------------------------- |
| `private_data` | **驱动私有数据**，通常由设备驱动用于存储自定义信息 |
| `f_version`    | 版本号，用于检测文件操作冲突                       |
| `f_security`   | 安全模块（如 SELinux）的扩展数据                   |
| `f_ep_links`   | 用于 `epoll` 文件描述符的链表                      |



### 4、关键结构关系

![image-20250423211626135](E:\embedded_learning\embedded_driver_code_management\src\00_hello_drv\image-20250423211626135.png)



## 二、驱动程序编写流程

- 确定主设备号，也可以让内核分配(注册时传入0)：
  如果register_chrdev传入的主设备号为0，则在__register_chrdev_region中会分配主设备号；
- 定义驱动自己的file_operations结构体；
- 实现对应的 open/read/write 等函数，填入 file_operations 结构体；
- 把 file_operations 结构体告诉内核：register_chrdev
  注册好的字符设备会被添加（cdev_add函数）到cdev_map里面，可以通过cdev_get检索其中的字符设备
- 其他完善：提供设备信息，自动创建设备节点：class_create,  device_create
- 指明当前驱动的入口函数，安装驱动程序(insmod)时，就会去调用这个入口函数；
  module_init(hello_drv_init);
- 指明当前驱动的入口函数，卸载驱动程序(rmmod)时，调用出口函数，进而执行unregister_chrdev;
  module_exit(hello_drv_exit);
- 完善证书(GPL)、作者、驱动简介等信息；



示例代码如下：

```c
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/tty.h>
#include <linux/kmod.h>
#include <linux/gfp.h>


// 8、完善证书(GPL)、作者、驱动简介等信息
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kline <kline_code@example,com>");
MODULE_DESCRIPTION("Test for hello driver");

#define MAX_DRV_CAHCE_LEN (1024)
#define MIN(x, y) x < y ? x : y
#define DEBUG_LOG(fmt, ...) \
        printk(KERN_INFO "[%s line%d] %s(): "fmt"\n", \
            KBUILD_BASENAME, __LINE__, __FUNCTION__, ##__VA_ARGS__)

// 1、 确定主设备号，也可以让内核分配(注册时传入0);
static int major = 0;
static struct class *hello_drv_class = NULL;

// 3、 实现对应的 open/read/write 等函数，填入 file_operations 结构体；
static int hello_drv_open(struct inode *inode, struct file *file)
{
    return 0;
}

static ssize_t hello_drv_read(struct file *file, char __user *buf, size_t size, loff_t *offset)
{
    copy_to_user(buf, g_cache_buf + *offset, MIN(size, MAX_DRV_CAHCE_LEN - *offset));
    return 1;
}

static ssize_t hello_drv_write(struct file *file, const char __user *buf, size_t size, loff_t *offset)
{
    copy_from_user(g_cp_buf, buf, MIN(size, MAX_DRV_CAHCE_LEN));
    return 1;
}

// 2、 定义驱动自己的file_operations结构体；
static struct file_operations hello_drv_fop = {
    .owner =            THIS_MODULE,
    .open =             hello_drv_open,
    .read =             hello_drv_read,
    .write =            hello_drv_write,
};


// 7、 其他完善：提供设备信息，自动创建设备节点：class_create, device_create
static int __init hello_drv_init(void)
{
    int err;
    struct device *hello_drv_device;

// 4、 把 file_operations 结构体告诉内核：register_chrdev
    // register
    major = register_chrdev(0, "hello_drv", &hello_drv_fop);
    if (major < 0) {
        DEBUG_LOG("Init hello drv failed!");
        return -1;
    }

// 5、 其他完善：提供设备信息，自动创建设备节点：class_create,  device_create
    // class create
	hello_drv_class = class_create(THIS_MODULE, "hello_drv");
    err = PTR_ERR(hello_drv_class);
	if (IS_ERR(hello_drv_class)) {
        DEBUG_LOG("Create hello drv class failed!");
        return -1;
    }

    // device create: /dev/hello_drv
    hello_drv_device = device_create(hello_drv_class, NULL, MKDEV(major, 0), NULL, "hello_drv");
    return 0;
}

static void __exit hello_drv_exit(void)
{
    device_destroy(hello_drv_class, MKDEV(major, 0));
    class_destroy(hello_drv_class);
	unregister_chrdev(major, "hello_drv");
}

// 6、指明当前驱动的入口函数，安装驱动程序(insmod)时，就会去调用这个入口函数；
module_init(hello_drv_init);
// 7、指明当前驱动的入口函数，卸载驱动程序(rmmod)时，调用出口函数，进而执行unregister_chrdev;
module_exit(hello_drv_exit);

```



笔记：

- 在内核编译中，可以用KBUILD_BASENAME来代替\_\_FILE\_\_宏，表示当前文件名；
- 不能够直接在内核的代码中访问用户空间的buf，例如read/write函数传入的buf：
  char __user *buf
  需要通过copy_to_user/copy_from_user来访问，否则编译时可以通过，但是运行时会导致内核崩溃，造成系统重启；



## 三、装载驱动和调试方法

### 1、安装驱动

insmod <module_name>

exp：

insmod hello_drv.ko



### 2、卸载驱动

rmmod <module_name>

exp：

rmmod hello_drv



### 3、检查驱动效果

- lsmod命令：检查当前kernel安装的驱动有哪些；
- 查看/dev下是否有指定名称的设备文件，需要在注册驱动时调用了device_create函数；如/dev/hello_drv；
- 可以通过App中读写来调试，添加打印看看是否进入了自定义的函数；

