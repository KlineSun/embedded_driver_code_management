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
- 实现对应的 drv_open/drv_read/drv_write 等函数，填入 file_operations 结构体；
- 把 file_operations 结构体告诉内核：register_chrdev
- 谁来注册驱动程序啊？得有一个入口函数：安装驱动程序时，就会去调用这个入口函数；
- 有入口函数就应该有出口函数：卸载驱动程序时，出口函数调用unregister_chrdev;
- 其他完善：提供设备信息，自动创建设备节点：class_create,  device_create





