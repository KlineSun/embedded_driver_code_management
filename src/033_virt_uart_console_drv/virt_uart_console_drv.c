#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/kmod.h>
#include <asm/irq.h>
#include <linux/irq.h>
#include <asm/io.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/interrupt.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_core.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kline <kline_code@example,com>");
MODULE_DESCRIPTION("Test for virtual uart.");

#define DEBUG_LOG(fmt, ...) \
        printk(KERN_INFO "[%s line%d] %s(): "fmt"\n", \
            KBUILD_BASENAME, __LINE__, __FUNCTION__, ##__VA_ARGS__)

#define MAX_VIRT_UART_NUM  (4)
#define MAX_TELE_DEV_CACHE (2048)
#define DRIVER_NAME "STM32-uart"
#define DEV_NAME "ttyvuart"
#define TELE_DEV_NAME "tele_uart"


struct stm32_circle_buf {
    unsigned char *buf;
    unsigned size;
    unsigned filled;
    unsigned head;
    unsigned tail;

    #define BUF_BUSY (1)
    #define BUF_FREE (2)
    unsigned char state;
    bool inited;
    unsigned long lock_flags;
    spinlock_t lock;

    #define EEMPTY (999)
    int  (*init)(struct stm32_circle_buf *cbuf, int size);
    void (*destroy)(struct stm32_circle_buf *cbuf);
    bool (*is_empty)(struct stm32_circle_buf *cbuf);
    bool (*is_full)(struct stm32_circle_buf *cbuf);
    int  (*write_byte)(struct stm32_circle_buf *cbuf, u8 val);
    int   (*read_byte)(struct stm32_circle_buf *cbuf, u8 *val);
    int  (*write_block)(struct stm32_circle_buf *cbuf, u8 *block, int size);
    int  (*read_block)(struct stm32_circle_buf *cbuf, u8 *block, int size);
    int  (*read_all)(struct stm32_circle_buf *cbuf, u8 *buf, int max_size);
};

struct tele_uart_device {
    char name[32];
    struct stm32_circle_buf *cbuf;
    spinlock_t lock; //用于proc_fp中的自旋锁
    struct proc_dir_entry *proc_fp;
};

struct virt_uart_port {
    struct uart_port port;
    int rx_irq;
    int tx_irq;
    struct tele_uart_device *tele_dev;
};

static struct virt_uart_port *g_virt_port;
// 模拟远端串口设备，与当前驱动实现数据交互
static struct tele_uart_device *g_tele_uart_dev;

static struct uart_driver virt_uart_driver;

/*-----------------------------------------------circle_buf_begin-----------------------------------------------------------------*/
void circle_buf_transfer_lock(struct stm32_circle_buf *cbuf)
{
    if (!cbuf || !cbuf->inited) {
        DEBUG_LOG("Invalid parameter!");
        return;
    }
    //DEBUG_LOG("Enter!");
    spin_lock_irqsave(&cbuf->lock, cbuf->lock_flags);
    cbuf->state = BUF_BUSY;
}

void circle_buf_transfer_unlock(struct stm32_circle_buf *cbuf)
{
    if (!cbuf || !cbuf->inited) {
        DEBUG_LOG("Invalid parameter!");
        return;
    }
    //DEBUG_LOG("Enter!");
    cbuf->state = BUF_FREE;
    spin_unlock_irqrestore(&cbuf->lock, cbuf->lock_flags);
}


int  circle_buf_init(struct stm32_circle_buf *cbuf, int size)
{
    DEBUG_LOG("Enter!");
    // 依据size创建buf
    cbuf->buf = kzalloc(size, GFP_KERNEL);
    if (!cbuf->buf) {
        DEBUG_LOG("Alloc cirecle buf memory failed!");
        return -ENOMEM;
    }

    // 初始化自旋锁
    spin_lock_init(&cbuf->lock);
    cbuf->size = size;
    cbuf->filled = 0;
    cbuf->inited = true;
    return 0;
}
void circle_buf_destroy(struct stm32_circle_buf *cbuf)
{
    DEBUG_LOG("Enter!");
    if (cbuf->buf) {
        kfree(cbuf->buf);
    }
    cbuf->inited = false;
    return;
}
bool circle_buf_is_empty(struct stm32_circle_buf *cbuf)
{
    if (!cbuf || !cbuf->inited) {
        DEBUG_LOG("Invalid parameter!");
        return true;
    }

    DEBUG_LOG("filled=%d!", cbuf->filled);
    return cbuf->filled == 0;
}
bool circle_buf_is_full(struct stm32_circle_buf *cbuf)
{
    if (!cbuf || !cbuf->inited) {
        DEBUG_LOG("Invalid parameter!");
        return false;
    }

    DEBUG_LOG("filled=%d, size=%d!", cbuf->filled, cbuf->size);
    return cbuf->filled == cbuf->size;
}
int  circle_buf_write_byte(struct stm32_circle_buf *cbuf, u8 val)
{
    if (!cbuf || !cbuf->inited) {
        DEBUG_LOG("Invalid parameter!");
        return -EINVAL;
    }

    // DEBUG_LOG("Enter!");
    circle_buf_transfer_lock(cbuf);
    if (circle_buf_is_full(cbuf)) {
        DEBUG_LOG("circle buffer is full, data is overriding!");
        cbuf->head++; // data head move to next.
        // return -EINVAL;
    }
    cbuf->buf[cbuf->tail] = val;
    // index revert to 0 if index up to max.
    if (cbuf->tail == cbuf->size - 1)
        cbuf->tail = 0;
    else
        cbuf->tail++;
    if (cbuf->filled < cbuf->size)
        cbuf->filled++;
    circle_buf_transfer_unlock(cbuf);
    DEBUG_LOG("head=%d, tail=%d, filled=%d!", cbuf->head, cbuf->tail, cbuf->filled);
    return 1;
}
int   circle_buf_read_byte(struct stm32_circle_buf *cbuf, u8 *val)
{
    if (!cbuf || !cbuf->inited) {
        DEBUG_LOG("Invalid parameter!");
        return -EINVAL;
    }

    //DEBUG_LOG("Enter!");
    circle_buf_transfer_lock(cbuf);
    if (circle_buf_is_empty(cbuf)) {
        DEBUG_LOG("circle buffer is empty!");
        circle_buf_transfer_unlock(cbuf);
        return -EEMPTY;
    }
    *val = cbuf->buf[cbuf->head];
    cbuf->buf[cbuf->head] = 0;
    // index revert to 0 if index up to max.
    if (cbuf->head == cbuf->size - 1)
        cbuf->head = 0;
    else
        cbuf->head++;
    cbuf->filled--;
    circle_buf_transfer_unlock(cbuf);
    DEBUG_LOG("head=%d, tail=%d, filled=%d!", cbuf->head, cbuf->tail, cbuf->filled);
    return 0;
}
int  circle_buf_write_block(struct stm32_circle_buf *cbuf, u8 *block, int size)
{
    int i = 0;
    if (!cbuf || !cbuf->inited || size > cbuf->size) {
        DEBUG_LOG("Invalid parameter!");
        return -EINVAL;
    }

    DEBUG_LOG("Enter!");
    for (i = 0; i < size; i++) {
        if (circle_buf_write_byte(cbuf, block[i]) < 0) {
            DEBUG_LOG("write byte failed!");
            return -1;
        }
    }
    return i;
}
int   circle_buf_read_block(struct stm32_circle_buf *cbuf, u8 *block, int size)
{
    int i = 0, err = 0;
    u8 val;
    if (!cbuf || !cbuf->inited || size > cbuf->size) {
        DEBUG_LOG("Invalid parameter!");
        return -EINVAL;
    }
    DEBUG_LOG("Enter!");
    for (i = 0; i < size; i++) {
        err = circle_buf_read_byte(cbuf, &val);
        if (err < 0) {
            if (err == -EEMPTY)
                break;
            DEBUG_LOG("Read byte failed!");
            return -1;
        }
        block[i] = val;
    }
    DEBUG_LOG("Read block: %s", block);
    return i;
}

int  circle_buf_read_all(struct stm32_circle_buf *cbuf, u8 *buf, int max_size)
{
    int i = 0, err = 0, size = 0;
    u8 val = 0;
    if (!cbuf || !cbuf->inited || size > cbuf->size) {
        DEBUG_LOG("Invalid parameter!");
        return -EINVAL;
    }

    size = max_size < cbuf->filled ? max_size : cbuf->filled;
    DEBUG_LOG("Read size: %d!", size);
    for (i = 0; i < size; i++) {
        err = circle_buf_read_byte(cbuf, &val);
        if (err < 0) {
            if (err == -EEMPTY)
                break;
            DEBUG_LOG("Read byte failed!");
            return -1;
        }
        buf[i] = val;
    }
    DEBUG_LOG("Read buffer: %s", buf);
    return i;
}

static struct stm32_circle_buf tele_dev_cbuf = {
    .state      = BUF_FREE,
    .inited     = false,
    .init       = circle_buf_init,
    .destroy    = circle_buf_destroy,
    .is_empty   = circle_buf_is_empty,
    .is_full    = circle_buf_is_full,
    .write_byte = circle_buf_write_byte,
    .read_byte  = circle_buf_read_byte,
    .write_block    = circle_buf_write_block,
    .read_block     = circle_buf_read_block,
    .read_all       = circle_buf_read_all,
};
/*-----------------------------------------------circle_buf_end-----------------------------------------------------------------*/

/*-----------------------------------------------uart_ops_begin-----------------------------------------------------------------*/
static struct virt_uart_port *get_virt_uart_port(struct uart_port *port)
{
    return container_of(port, struct virt_uart_port, port);
}

static unsigned int virt_uart_tx_empty(struct uart_port *port)
{
    struct virt_uart_port *vport = get_virt_uart_port(port);
    struct stm32_circle_buf *cbuf = NULL;
    if (!vport || !vport->tele_dev->cbuf) {
        DEBUG_LOG("Enter!");
        return -1;
    }

    DEBUG_LOG("Enter!");
    cbuf = vport->tele_dev->cbuf;
    if (cbuf->state == BUF_BUSY)
        return 0;
    else if (cbuf->state == BUF_FREE)
        return 1;

    return 1;
}
static void virt_uart_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
    DEBUG_LOG("Enter with mctrl: %d!", mctrl);
    return;
}
static unsigned int virt_uart_get_mctrl(struct uart_port *port)
{
    DEBUG_LOG("Enter!");
    return 0;
}
static void virt_uart_stop_tx(struct uart_port *port)
{
    DEBUG_LOG("Enter!");
    return;
}
static void virt_uart_start_tx(struct uart_port *port)
{
    struct virt_uart_port *vport = get_virt_uart_port(port);
    DEBUG_LOG("Enter!");

    // 触发中断，在中断处理函数中完成数据发送
    irq_set_irqchip_state(vport->tx_irq, IRQCHIP_STATE_PENDING, true);
    DEBUG_LOG("Trigger tx_irq for transmit data!");
    return;
}
static void virt_uart_stop_rx(struct uart_port *port)
{
    DEBUG_LOG("Enter!");
    return;
}
static void virt_uart_enable_ms(struct uart_port *port)
{
    DEBUG_LOG("Enter!");
    return;
}
static void virt_uart_break_ctl(struct uart_port *port, int ctl)
{
    DEBUG_LOG("Enter!");
    return;
}
static int virt_uart_startup(struct uart_port *port)
{
    DEBUG_LOG("Enter!");
    return 0;
}
static void virt_uart_shutdown(struct uart_port *port)
{
    DEBUG_LOG("Enter!");
    return;
}
static void virt_uart_flush_buffer(struct uart_port *port)
{
    DEBUG_LOG("Enter!");
    return;
}
static void virt_uart_set_termios(struct uart_port *port, struct ktermios *new,
                    struct ktermios *old)
{
    DEBUG_LOG("Enter!");
    return;
}
static const char *virt_uart_type(struct uart_port *port)
{
    DEBUG_LOG("Enter!");
    return DRIVER_NAME;
}
static void virt_uart_config_port(struct uart_port *port, int flags)
{
    DEBUG_LOG("Enter!");
    if (flags & UART_CONFIG_TYPE)
        port->type = PORT_STM32;
    return;
}
static int virt_uart_verify_port(struct uart_port *port, struct serial_struct *ser)
{
    DEBUG_LOG("Enter!");
    return 0;
}

static const struct uart_ops virt_uart_pops = {
	.tx_empty	= virt_uart_tx_empty,
	.set_mctrl	= virt_uart_set_mctrl,
	.get_mctrl	= virt_uart_get_mctrl,
	.stop_tx	= virt_uart_stop_tx,
	.start_tx	= virt_uart_start_tx,
	.stop_rx	= virt_uart_stop_rx,
	.enable_ms	= virt_uart_enable_ms,
	.break_ctl	= virt_uart_break_ctl,
	.startup	= virt_uart_startup,
	.shutdown	= virt_uart_shutdown,
	.flush_buffer	= virt_uart_flush_buffer,
	.set_termios	= virt_uart_set_termios,
	.type		= virt_uart_type,
	.config_port	= virt_uart_config_port,
	.verify_port	= virt_uart_verify_port,
};
/*-----------------------------------------------uart_ops_end-----------------------------------------------------------------*/

/*-----------------------------------------------uart_driver_begin-----------------------------------------------------------------*/
struct tty_driver *virt_uart_console_device(struct console *co, int *index)
{
	struct uart_driver *p = co->data;

    DEBUG_LOG("Enter!");
	*index = co->index;
	return p->tty_driver;
}

static void virt_uart_console_write(struct console *co, const char *s, unsigned int cnt)
{
    struct uart_driver *drv = co->data;
    struct uart_port *port = NULL;
    struct virt_uart_port *vport = NULL;
    struct stm32_circle_buf *cbuf = NULL;
    int ret = 0;

    if (!drv || co->index == -1 || co->index >= drv->nr) {
        DEBUG_LOG("Invalid parameter!");
        return;
    }
    DEBUG_LOG("Enter with console name=%s, index=%d, cnt=%d", co->name, co->index, cnt);

    // find vport and cbuf
    if (!(port = drv->state[co->index].uart_port)
        || !(vport = container_of(port, struct virt_uart_port, port))
        || !(cbuf = vport->tele_dev->cbuf)) {
        DEBUG_LOG("Invalid state index!");
        return;
    }

    ret = cbuf->write_block(cbuf, (u8 *)s, cnt);
    if (ret <= 0) {
        DEBUG_LOG("Write data to fifo failed!");
        return;
    }
    DEBUG_LOG("Console write success!");
}

static struct console virt_uart_console = {
	.name		= DEV_NAME,
	.device		= virt_uart_console_device,
	.write		= virt_uart_console_write,
	.flags		= CON_PRINTBUFFER,
	.index		= -1, // -1表示由cmdline决定使用哪个console
	.data		= &virt_uart_driver,
};

static struct uart_driver virt_uart_driver = {
    .owner          = THIS_MODULE,
    .driver_name    = DRIVER_NAME,
    .dev_name       = DEV_NAME,
    .major          = 0,
    .minor          = 0,
    .nr             = MAX_VIRT_UART_NUM,
    .cons           = &virt_uart_console,
};
/*-----------------------------------------------uart_driver_end-----------------------------------------------------------------*/


/*-----------------------------------------------file_ops_begin-----------------------------------------------------------------*/
static int tele_uart_drv_open(struct inode *inode, struct file *file)
{
    int minor = iminor(inode);

    DEBUG_LOG("Enter with minor: %d", minor);
    return 0;
}

static ssize_t tele_uart_drv_write(struct file *file, const char __user *buf, size_t size, loff_t *offset)
{
    char bit_val = 0;
    char *write_buf = NULL;
    struct inode *inode = file_inode(file);
    int minor = iminor(inode);
    struct virt_uart_port *vport = PDE_DATA(inode);
    struct stm32_circle_buf *cbuf = NULL;
    int ret = 0;

    if (size <= 0 || !vport || !vport->tele_dev || !vport->tele_dev->cbuf) {
        DEBUG_LOG("Invalid paramter!");
        return -EPERM;
    }
    cbuf = vport->tele_dev->cbuf;
    DEBUG_LOG("Enter with minor=%d, tele_uart_dev=%s", minor, vport->tele_dev->name);

    // write data into circle_buf
    if (size == 1) {
        copy_from_user(&bit_val, buf, size);
        ret = cbuf->write_byte(cbuf, bit_val);
        if (ret < 0) {
            DEBUG_LOG("Write byte failed!");
            return ret;
        }
    } else {
        write_buf = kzalloc(size + 1, GFP_KERNEL);
        if (!write_buf) {
            DEBUG_LOG("Enter!");
            return -ENOMEM;
        }
        copy_from_user(write_buf, buf, size);
        write_buf[size] = 0;

        ret = cbuf->write_block(cbuf, write_buf, size);
        if (ret < 0) 
            DEBUG_LOG("Write block failed!");
        kfree(write_buf);
    }

    // trigger interrupt: rx_irq
    irq_set_irqchip_state(vport->rx_irq, IRQCHIP_STATE_PENDING, true);

    return ret;
}

static ssize_t tele_uart_drv_read(struct file *file, char __user *buf, size_t size, loff_t *offset)
{
    u8 bit_val = 0;
    char *read_buf = NULL;
    struct inode *inode = file_inode(file);
    int minor = iminor(inode);
    struct virt_uart_port *vport = PDE_DATA(inode);
    struct stm32_circle_buf *cbuf = NULL;
    int ret = 0, read_size = 0;

    if (size <= 0 || !vport || !vport->tele_dev || !vport->tele_dev->cbuf) {
        DEBUG_LOG("Invalid paramter!");
        return -EPERM;
    }
    cbuf = vport->tele_dev->cbuf;
    DEBUG_LOG("Enter with minor=%d, tele_uart_dev=/proc/%s, size=%d", minor, vport->tele_dev->name, size);

    if (cbuf->is_empty(cbuf)) {
        DEBUG_LOG("No data in circle_buf!");
        return 0;
    }

    // write data into circle_buf
    if (size == 1) {
        ret = cbuf->read_byte(cbuf, &bit_val);
        if (ret < 0) {
            DEBUG_LOG("Write byte failed!");
            return ret;
        }
        copy_to_user(buf, &bit_val, size);
    } else {
        read_size = size < cbuf->size ? size : cbuf->size;
        read_buf = kzalloc(read_size + 1, GFP_KERNEL);
        if (!read_buf) {
            DEBUG_LOG("Enter!");
            return -ENOMEM;
        }
        read_buf[read_size] = 0;

        ret = cbuf->read_block(cbuf, read_buf, read_size);
        if (ret < 0) 
            DEBUG_LOG("Read block failed!");

        DEBUG_LOG("Read data: %s", read_buf);
        copy_to_user(buf, read_buf, ret);
        kfree(read_buf);
    }

    if (cbuf->is_empty) {
        // trigger interrupt: tx_irq, 表示当前FIFO为空，触发中断检查行规程xmit是否有数据
        irq_set_irqchip_state(vport->tx_irq, IRQCHIP_STATE_PENDING, true);
    }

    return ret;
}

static int tele_uart_drv_close(struct inode *inode, struct file *file)
{
    int minor = iminor(inode);

    DEBUG_LOG("Enter with minor: %d", minor);
    return 0;
}

static const struct file_operations tele_uart_file_ops = {
    .owner = THIS_MODULE,
    .open =             tele_uart_drv_open,
    .read =             tele_uart_drv_read,
    .write =            tele_uart_drv_write,
    .release =          tele_uart_drv_close,
};
/*-----------------------------------------------file_ops_end-----------------------------------------------------------------*/


/*-----------------------------------------------isr_begin-----------------------------------------------------------------*/
static irqreturn_t virt_uart_rx_isr(int irq, void *dev_id)
{
    struct virt_uart_port *vport = dev_id;
    struct stm32_circle_buf *cbuf = NULL;
    struct tty_port *tport = NULL;
    char *rx_buf = NULL;
    int ret = 0, w_bytes = 0;
    if (!vport || !vport->tele_dev) {
        DEBUG_LOG("Invalid parameter!");
        return -EINVAL;
    }

    cbuf = vport->tele_dev->cbuf;
    tport = &vport->port.state->port;
    if (!cbuf || !tport) {
        DEBUG_LOG("Invalid cbuf or tport!");
        return -EINVAL;
    }

    // DEBUG_LOG("Enter!");
    rx_buf = kzalloc(cbuf->filled + 1, GFP_KERNEL);
    if (!rx_buf) {
        DEBUG_LOG("Alloc rx_buf memory failed!");
        return -ENOMEM;
    }

    // 从fifo(即circle_buf)中读取数据
    ret = cbuf->read_all(cbuf, rx_buf, cbuf->filled);
    if (ret < 0) {
        DEBUG_LOG("Read all data from circle_buf failed!");
        goto out_free;
    }
    DEBUG_LOG("Read buf: %s, size=%d", rx_buf, ret);

    // 将数据刷到行规程的xmit中
    w_bytes = tty_insert_flip_string(tport, rx_buf, ret);

    // 通知行规程处理
    if (w_bytes) {
        tty_flip_buffer_push(tport);
        DEBUG_LOG("We get %d bytes.", w_bytes);
	}

out_free:
    if (rx_buf)
        kfree(rx_buf);

    return IRQ_HANDLED;
}


static irqreturn_t virt_uart_tx_isr(int irq, void *dev_id)
{
    struct virt_uart_port *vport = dev_id;
    struct circ_buf *xmit = NULL;
    struct stm32_circle_buf *cbuf = NULL;

    if (!vport) {
        DEBUG_LOG("Invalid parameter!");
        return IRQ_NONE;
    }

    DEBUG_LOG("Enter tdx_irq!");
    // 从xmit中取出数据
    xmit = &vport->port.state->xmit;
    cbuf = vport->tele_dev->cbuf;

    // 将数据保存在circle_buf中
    // 如果xmit数据为空，直接返回
    if (uart_circ_empty(xmit)/*|| uart_tx_stopped(&vport->port)*/) {
        DEBUG_LOG("No data in xmit!");
        return IRQ_HANDLED;
    }

    while (!uart_circ_empty(xmit)) {
        // 逐位将数据保存在circle_buf中
        cbuf->write_byte(cbuf, xmit->buf[xmit->tail]);
        xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
        vport->port.icount.tx++;
    }

    // 唤醒等待数据的进程
    if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
        uart_write_wakeup(&vport->port);

    // if (uart_circ_empty(xmit))
    //     imx_uart_stop_tx(&vport->port);
    DEBUG_LOG("End tdx_irq!");
    return IRQ_HANDLED;
}
/*-----------------------------------------------isr_end-----------------------------------------------------------------*/


int virtual_uart_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    struct device_node	*pn = pdev->dev.of_node;
    u32 id = 0;
    int err = 0;

    if (!dev || !pn) {
        DEBUG_LOG("Invalid device node!");
        return -EINVAL;
    }

    DEBUG_LOG("Enter!");
    g_virt_port = devm_kzalloc(dev, sizeof(*g_virt_port), GFP_KERNEL);
    if (!g_virt_port) {
        DEBUG_LOG("Alloc virt_uart_port memory failed!");
        return -ENOMEM;
    }

    g_tele_uart_dev = devm_kzalloc(dev, sizeof(*g_tele_uart_dev), GFP_KERNEL);
    if (!g_tele_uart_dev) {
        DEBUG_LOG("Alloc tele_uart_device memory failed!");
        return -ENOMEM;
    }
    g_tele_uart_dev->cbuf = &tele_dev_cbuf;
    snprintf(g_tele_uart_dev->name, sizeof(g_tele_uart_dev->name), "%s", TELE_DEV_NAME);
    // 构建联系
    g_virt_port->tele_dev = g_tele_uart_dev;

    if (of_property_read_u32(pn, "port_index", &id)) {
        DEBUG_LOG("Read port index failed!");
        return -EINVAL;
    }

    // 获取中断
    g_virt_port->rx_irq = platform_get_irq(pdev, 0);
    g_virt_port->tx_irq = platform_get_irq(pdev, 1);
    if (g_virt_port->rx_irq < 0 || g_virt_port->tx_irq < 0) {
        DEBUG_LOG("Get irq failed: rx_irq=%d, tx_irq=%d", g_virt_port->rx_irq, g_virt_port->tx_irq);
        return -EINVAL;
    }
    DEBUG_LOG("Get rx_irq=%d, tx_irq=%d", g_virt_port->rx_irq, g_virt_port->tx_irq);

    g_virt_port->port.dev = &pdev->dev;
    g_virt_port->port.type = PORT_STM32,
    g_virt_port->port.iotype = UPIO_MEM;
    g_virt_port->port.iobase = 1; // for uart_configure_port register console
    g_virt_port->port.irq = g_virt_port->rx_irq;
    g_virt_port->port.fifosize = 32;
    g_virt_port->port.line = id;
    g_virt_port->port.ops = &virt_uart_pops;
    g_virt_port->port.flags = UPF_BOOT_AUTOCONF;
    DEBUG_LOG("Max port number=%d, current id=%d", MAX_VIRT_UART_NUM, id);

    // 申请中断
    err = devm_request_irq(dev, g_virt_port->rx_irq, virt_uart_rx_isr, 0, dev_name(&pdev->dev), g_virt_port);
    if (err) {
        DEBUG_LOG("Request rx irq failed: %d", err);
        return -EINVAL;
    }
    err = devm_request_irq(dev, g_virt_port->tx_irq, virt_uart_tx_isr, 0, dev_name(&pdev->dev), g_virt_port);
    if (err) {
        DEBUG_LOG("Request tx irq failed: %d", err);
        return -EINVAL;
    }

    err = g_tele_uart_dev->cbuf->init(g_tele_uart_dev->cbuf, MAX_TELE_DEV_CACHE);
    if (err) {
        DEBUG_LOG("Init tele uart device failed: %d", err);
        return -EINVAL;
    }

    DEBUG_LOG("create /proc/%s", g_tele_uart_dev->name);
    g_tele_uart_dev->proc_fp = proc_create_data(g_tele_uart_dev->name,
                                                0777,
                                                NULL,
                                                &tele_uart_file_ops,
                                                g_virt_port);
    if (!g_tele_uart_dev->proc_fp) {
        DEBUG_LOG("Create process file!");
        err = -EINVAL;
        goto circle_buf_free;
    }

    platform_set_drvdata(pdev, g_virt_port);

    err = uart_add_one_port(&virt_uart_driver, &g_virt_port->port);
    if (err) {
        DEBUG_LOG("Add uart_port failed: %d", err);
        goto  proc_fs_free;
    }
    DEBUG_LOG("Probe button success!");
    return 0;

proc_fs_free:
    if (g_tele_uart_dev->proc_fp)
        proc_remove(g_tele_uart_dev->proc_fp);

circle_buf_free:
    if (g_tele_uart_dev->cbuf->inited)
        g_tele_uart_dev->cbuf->destroy(g_tele_uart_dev->cbuf);
    return err;
}

int virtual_uart_remove(struct platform_device *pdev)
{
    DEBUG_LOG("Enter!");

    uart_remove_one_port(&virt_uart_driver, &g_virt_port->port);
    proc_remove(g_tele_uart_dev->proc_fp);
    g_tele_uart_dev->cbuf->destroy(g_tele_uart_dev->cbuf);
    DEBUG_LOG("End!");
    return 0;
}

static const struct of_device_id ask100_virt_uart_of_match[] = {
	{ .compatible = "100ask,virt_uart_drv", },
	{},
};

static struct platform_driver virt_uart_platform_driver = {
	.probe		= virtual_uart_probe,
	.remove		= virtual_uart_remove,
	.driver		= {
		.name	= "100ask_virt_uart",
        .of_match_table = ask100_virt_uart_of_match,
	},
};

static int __init virt_uart_init(void)
{
    int ret = 0;

    DEBUG_LOG("Enter!");
    ret = uart_register_driver(&virt_uart_driver);
    if (ret < 0) {
        DEBUG_LOG("Register uart driver failed!");
        return ret;
    }

    ret = platform_driver_register(&virt_uart_platform_driver);
    if (ret != 0) {
        DEBUG_LOG("Register platform driver failed!");
        uart_unregister_driver(&virt_uart_driver);
    }

    return ret;
}

static void __exit virt_uart_exit(void)
{
    platform_driver_unregister(&virt_uart_platform_driver);
    uart_unregister_driver(&virt_uart_driver);
}

module_init(virt_uart_init);
module_exit(virt_uart_exit);
