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
#include <asm/io.h>
#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/interrupt.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/wait.h>
#include <linux/jiffies.h>
#include <linux/poll.h>
#include <linux/timer.h>
#include "button_irq_tasklet_drv.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kline <kline_code@example,com>");
MODULE_DESCRIPTION("Test for button bus driver");

#define MAX_CACHE_EVENT_COUNT (32)
#define MAX_USER_BUF_LEN      (1024)
#define KEY_SMOOTH_TIME_MS    (50)
#define TIME_IN_HZ(tm)        ((tm / 1000) * HZ)

struct local_button {
    int btn_num;
    int gpio_num;
    int irq;
    enum of_gpio_flags flags;
    char label[32];
    struct gpio_desc *gpiod;
    struct fasync_struct *fasync;
    struct timer_list timer;
    struct tasklet_struct	tasklet;
};

struct button_irq_event {
    struct local_button *btn;
    int idx;
    int val;
    unsigned long tm;
    struct button_irq_event *next;
    struct button_irq_event *pre;
};

static int major = 0;
static struct class *button_drv_class = NULL;
static struct local_button *g_btn_list = NULL;
static int btn_irq_cnt = 0;
static int g_wq_key = 0;
static DECLARE_WAIT_QUEUE_HEAD(ask100_button_wq);

// cache buf
static int global_irq_idx = 0;
// double_dir circle list
static struct button_irq_event *btn_events_clist = NULL;
static int cache_event_cnt = 0;
static DEFINE_SPINLOCK(btn_event_lock); // 全局锁

//static struct button_irq_event prealloc_event; // 预分配的事

static int push_event(struct button_irq_event *event)
{
    struct button_irq_event *ev = NULL;
    if (!event) {
        DEBUG_LOG("Invalid event pointer!");
        return -1;
    }

    spin_lock(&btn_event_lock);
     if (cache_event_cnt >= MAX_CACHE_EVENT_COUNT) {
        // If event counts exceeds limit
        DEBUG_LOG("Event counts exceeds limit, cover old node!");
        btn_events_clist->btn     = event->btn;
        btn_events_clist->idx     = event->idx;
        btn_events_clist->val     = event->val;
        btn_events_clist->tm      = event->tm;

        // move head to next.
        btn_events_clist = btn_events_clist->next;
        // cache_event_cnt = MAX_CACHE_EVENT_COUNT;
        spin_unlock(&btn_event_lock);
        return 0;
    }

    ev = kzalloc(sizeof(struct button_irq_event), GFP_ATOMIC);
    if (!ev) {
        DEBUG_LOG("Invalid event pointer!");
        return -1;
    }
    // fill in new struct
    memcpy(ev, event, sizeof(struct button_irq_event));

    // Is first event?
    DEBUG_LOG("Insert a new node: KEY%d", ev->btn->btn_num);
    if (!btn_events_clist) {
        btn_events_clist = ev;
        btn_events_clist->next = ev;
        btn_events_clist->pre  = ev;
    } else {
        // rebuild new node left chain
        btn_events_clist->pre->next = ev;
        ev->pre = btn_events_clist->pre;
        // rebuild new node right chain
        ev->next = btn_events_clist;
        btn_events_clist->pre = ev;
    }

    cache_event_cnt++;
    spin_unlock(&btn_event_lock);
    // DEBUG_LOG("End!");
    return 0;
}

int pop_event(int btn_num, struct button_irq_event* event)
{
    struct button_irq_event *tmp = btn_events_clist;
    bool is_found = false;
    if (!event || !tmp || cache_event_cnt <= 0) {
        DEBUG_LOG("Invalid event pointer!");
        return -1;
    }

    spin_lock(&btn_event_lock);
    do {
        if (tmp->btn->btn_num == btn_num) {
            DEBUG_LOG("Found event node, index: %d", tmp->idx);
            is_found = true;
            break;
        }
        tmp = tmp->next;
    } while (tmp != btn_events_clist);

    if (!is_found) {
        DEBUG_LOG("Not find event node as this button number: %d", btn_num);
        spin_unlock(&btn_event_lock);
        return -1;
    }

    // fill in
    memcpy(event, tmp, sizeof(struct button_irq_event));
    DEBUG_LOG("Pop a node: KEY%d", event->btn->btn_num);
    // unlink poped node
    cache_event_cnt--;
    tmp->pre->next = tmp->next;
    tmp->next->pre = tmp->pre;
    if (cache_event_cnt == 0) {
        btn_events_clist = NULL;
    } else if (tmp == btn_events_clist) {
        // if match head, move head to next
        btn_events_clist = tmp->next;
    }

    // free poped node
    kfree(tmp);
    spin_unlock(&btn_event_lock);
    //DEBUG_LOG("End!");
    return 0;
}

static int btn_event_count(int btn_num)
{
    struct button_irq_event *tmp = btn_events_clist;
    int count = 0;
    if (cache_event_cnt <=0 || !btn_events_clist) {
        DEBUG_LOG("No button event exist!");
        return 0;
    }

    do {
        if (tmp->btn->btn_num == btn_num)
            count++;
        // else
        //     DEBUG_LOG("Mismatch button num: current=%d, target=%d", tmp->btn->btn_num, btn_num);

        tmp = tmp->next;
    } while (tmp != btn_events_clist);

    DEBUG_LOG("Found %d events for KEY%d!", count, btn_num);
    return count;
}

static void print_usage(void)
{
    DEBUG_LOG("Usage:");
    DEBUG_LOG("insmod sequence: base_module  -> chip_module -> dtb replace");
    DEBUG_LOG("rmmod sequence:  dtb replace  -> chip_module -> base_module");
}

static int button_drv_open(struct inode *inode, struct file *file)
{
    int minor = iminor(inode);
    DEBUG_LOG("Enter with minor: %d", minor);

    file->private_data = &g_btn_list[minor];
    return 0;
}

static ssize_t button_drv_write(struct file *file, const char __user *buf, size_t size, loff_t *offset)
{
    char bit_val = 0;
    struct inode *inode = file_inode(file);
    int minor = iminor(inode);

    DEBUG_LOG("Enter with minor: %d", minor);
    copy_from_user(&bit_val, buf, 1);

    return 1;
}

static ssize_t button_drv_read(struct file *file, char __user *buf, size_t size, loff_t *offset)
{
    struct inode *inode = file_inode(file);
    int minor = iminor(inode);
    char *rsp = NULL;
    size_t rsp_len = MIN(size, MAX_USER_BUF_LEN);
    size_t remain_len = rsp_len;
    int  i = 0, ret = 0;
    struct button_irq_event ev;

    DEBUG_LOG("Enter with minor: %d, read size=%d", minor, size);

    // wait for irq
    // wait_event_interruptible(ask100_button_wq, g_wq_key);

    rsp = kzalloc(rsp_len+1, GFP_KERNEL);
    if (!rsp) {
        DEBUG_LOG("Alloc heap memory failed!");
        return 0;
    }

    // read event form circle list
    for (i = 0; i < cache_event_cnt; i++) {
        if (pop_event(g_btn_list[minor].btn_num, &ev)) {
            DEBUG_LOG("pop event failed!");
            if (strlen(rsp) == 0)
                snprintf(rsp, rsp_len, "No event occured!");
            break;
        }

        if (g_btn_list[minor].flags & OF_GPIO_ACTIVE_LOW) {
            ret = snprintf(rsp + strlen(rsp), remain_len, "%ld BTN%d %s\n",
                ev.tm,
                ev.btn->btn_num,
                ev.val ? "UP" : "DOWN");
        } else {
            ret = snprintf(rsp + strlen(rsp), remain_len, "%ld BTN%d %s\n",
                ev.tm,
                ev.btn->btn_num,
                ev.val ? "DOWN" : "UP");
        }

        if (ret < 0) {
            DEBUG_LOG("snprintf failed!");
            snprintf(rsp, rsp_len, "snprintf failed!");
            break;
        }
        remain_len -= ret;
        if (remain_len <= 0)
            break;
        
        memset(&ev, 0, sizeof(struct button_irq_event));
    }

    DEBUG_LOG("response message: \n%s", rsp);
    copy_to_user(buf, rsp, rsp_len);
    // reset key
    // g_wq_key = 0;
    kfree(rsp);
    return rsp_len - remain_len;
}

static long button_drv_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct inode *inode = file_inode(file);
    int minor = iminor(inode);

    DEBUG_LOG("Enter with minor: %d", minor);
    return 1;
}

static __poll_t button_drv_poll(struct file *file, poll_table * wait)
{
    struct inode *inode = file_inode(file);
    int minor = iminor(inode);
    DEBUG_LOG("Enter with minor: %d", minor);
	poll_wait(file, &ask100_button_wq, wait);

    // reset key
    g_wq_key = 0;

    // Check count of button events
    if (btn_event_count(g_btn_list[minor].btn_num) <= 0) {
        DEBUG_LOG("No button even!");
        return 0;
    } else {
        DEBUG_LOG("There are some button events!");
        return POLLIN | POLLRDNORM;
    }
}

static int button_drv_fasync(int fd, struct file *filp, int mode)
{
    struct inode *inode = file_inode(filp);
    int minor = iminor(inode);
    struct local_button *pbtn = (struct local_button *)filp->private_data;
    DEBUG_LOG("Enter with minor: %d", minor);
    DEBUG_LOG("Get private data, button number=%d", pbtn->btn_num);

    // 失败是返回值小于等于0
	return fasync_helper(fd, filp, mode, &pbtn->fasync);
}

static int button_drv_close(struct inode *inode, struct file *file)
{
    int minor = iminor(inode);

    DEBUG_LOG("Enter with minor: %d", minor);
    return 0;
}

static struct file_operations button_drv_fop = {
    .owner =            THIS_MODULE,
    .open =             button_drv_open,
    .read =             button_drv_read,
    .write =            button_drv_write,
    .unlocked_ioctl =   button_drv_ioctl,
    .poll =             button_drv_poll,
    .fasync =           button_drv_fasync,
    .release =          button_drv_close,
};

static void key_smooth_timer(struct timer_list *unused)
{
	struct local_button *btn = from_timer(btn, unused, timer);

    if (!btn) {
        DEBUG_LOG("Convert pointer from timer failed!");
        return;
    }

    DEBUG_LOG("Enter timer function with KEY%d", btn->btn_num);
}

// tasklet function
static void button_tasklet_func(unsigned long priv)
{
    struct local_button *btn = (struct local_button *)priv;
    struct button_irq_event ev;

    if (!btn) {
        DEBUG_LOG("Convert pointer from private data failed!");
        return;
    }

    ev.btn = btn;
    ev.idx = ++global_irq_idx;
    ev.val = gpiod_get_value(btn->gpiod);
    ev.tm  = jiffies;
    if (push_event(&ev)) {
        DEBUG_LOG("push irq event to circle list failed!");
    }

    // wake up wait_queue
    wake_up_interruptible(&ask100_button_wq);
    g_wq_key = 1;
    DEBUG_LOG("Wake up ask100_button_wq!");

    // send signal
    if (btn->fasync)
        kill_fasync(&(btn->fasync), SIGIO, POLL_IN);
    DEBUG_LOG("Kill fasync: SIGIO,POLL_IN");
}

// 中断上半部，不会被其他中断
static irqreturn_t button_irq_cb(int irqno, void *priv_data)
{
	struct local_button *btn = priv_data;

    if (!btn) {
        DEBUG_LOG("Invalid button pointer!");
    }
 
    DEBUG_LOG("trigger success: KEY%d, gpio%d, label<%s>, irq%d, flags%d",
        btn->btn_num,
        btn->gpio_num,
        btn->label,
        btn->irq,
        btn->flags);

    mod_timer(&(btn->timer), jiffies + TIME_IN_HZ(KEY_SMOOTH_TIME_MS));
    tasklet_schedule(&(btn->tasklet));
	return IRQ_HANDLED;
}

int stm32mp157_button_probe(struct platform_device *pdev)
{
    struct device_node	*pn = NULL, *chid = NULL;
    int err = 0, idx = 0, ret = 0;
    if (!pdev) {
        DEBUG_LOG("Invalid platform device!");
        return -1;
    }

    pn = pdev->dev.of_node;
    if (!pn) {
        DEBUG_LOG("Cannot get device node from platform device!");
        return -1;
    }
    DEBUG_LOG("get device_node: name=%s, path=%s, id=%d", pn->name, pn->full_name, pn->phandle);

    /* 注册字符设备节点和class，以及file_operrations结构体 -begin */
    print_usage();
    major = register_chrdev(0, "button_irq_drv", &button_drv_fop);
    if (major < 0) {
        DEBUG_LOG("register button char device failed: %d", major);
        return major;
    }
    DEBUG_LOG("get device major num: %d", major);
	button_drv_class = class_create(THIS_MODULE, "button_irq_drv");
	if (IS_ERR(button_drv_class)) {
        DEBUG_LOG("Create button drv class failed!");
        unregister_chrdev(major, "button_irq_drv");
        return PTR_ERR(button_drv_class);
    }
    /* 注册字符设备节点和class，以及file_operrations结构体 -end */

    btn_irq_cnt = of_get_child_count(pn);
    if (btn_irq_cnt <= 0) {
        DEBUG_LOG("get button count failed!");
        err = -1;
        goto probe_err;
    }

    g_btn_list = kzalloc(sizeof(struct local_button) * btn_irq_cnt, GFP_KERNEL);
    if (!g_btn_list) {
        DEBUG_LOG("alloc kernel memory faield!");
        err = -2;
        goto probe_err;
    }

    for_each_child_of_node(pn, chid) {
        const char *label;
        struct device *button_drv_device = NULL;
        of_property_read_string(chid, "label", &label);
        of_property_read_u32(chid, "num", &g_btn_list[idx].btn_num);
        snprintf(g_btn_list[idx].label, 32, "%s", label);

        // get flags
        g_btn_list[idx].gpio_num = of_get_named_gpio_flags(chid,
                                                          "button-gpios",
                                                          0,
                                                          &g_btn_list[idx].flags);
        if (g_btn_list[idx].gpio_num < 0) {
            DEBUG_LOG("Failed to get gpio flags: %d", g_btn_list[idx].gpio_num);
            continue;
        }

        // get gpio desc
        g_btn_list[idx].gpiod = gpio_to_desc(g_btn_list[idx].gpio_num);
        if (!g_btn_list[idx].gpiod) {
            DEBUG_LOG("Failed to get gpio desc!");
            continue;
        }

        // get irq number
        g_btn_list[idx].irq = gpiod_to_irq(g_btn_list[idx].gpiod);
        if (g_btn_list[idx].irq <= 0) {
            DEBUG_LOG("Failed to get gpio irq: %d", g_btn_list[idx].irq);
            continue;
        }

        // set gpio input mode
        ret = gpio_direction_input(g_btn_list[idx].gpio_num);
        if (ret < 0) {
            DEBUG_LOG("Failed to set GPIO input: %d", ret);
            continue;
        }

        // request interrupt
        ret = request_irq(g_btn_list[idx].irq, button_irq_cb, IRQF_TRIGGER_FALLING, g_btn_list[idx].label, &g_btn_list[idx]);
        if (ret) {
            DEBUG_LOG("request irq for %s failed: %d", g_btn_list[idx].label, ret);
            continue;
        }

        // setup timer
        timer_setup(&g_btn_list[idx].timer, key_smooth_timer, 0);
        g_btn_list[idx].timer.expires = jiffies + TIME_IN_HZ(KEY_SMOOTH_TIME_MS);
        add_timer(&g_btn_list[idx].timer);

        // init tasklet
        tasklet_init(&(g_btn_list[idx].tasklet), button_tasklet_func, (unsigned long)(&g_btn_list[idx]));

        // create devices
        button_drv_device = device_create(button_drv_class,
                                        NULL,
                                        MKDEV(major, idx),
                                        NULL, "%s_%d", pn->name, idx);
        if (IS_ERR(button_drv_device)) {
            DEBUG_LOG("Create device failed, minor: %d", idx);
            continue;
        }
        DEBUG_LOG("probe success: KEY%d, gpio%d, label<%s>, irq%d, flags%d",
                                                                g_btn_list[idx].btn_num,
                                                                g_btn_list[idx].gpio_num,
                                                                g_btn_list[idx].label,
                                                                g_btn_list[idx].irq,
                                                                g_btn_list[idx].flags);
        idx++;
    }

    if (idx != btn_irq_cnt) {
        DEBUG_LOG("someone device_node probe failed: %d", idx);
        err = -3;
        goto probe_err;
    }
    return 0;

probe_err:
    class_destroy(button_drv_class);
    unregister_chrdev(major, "button_irq_drv");
    return err;
}

int stm32mp157_button_remove(struct platform_device *pdev)
{
    int i = 0;
    DEBUG_LOG("Enter!");

    // 逐个释放资源
    if (cache_event_cnt > 0 && btn_events_clist) {
        // 解环
        btn_events_clist->pre->next = NULL;
        btn_events_clist->pre = NULL;

        // 链表释放
        while (btn_events_clist) {
            kfree(btn_events_clist);
            btn_events_clist = btn_events_clist->next;
        }
    }

    for (i = 0; i < btn_irq_cnt; i++) {
        DEBUG_LOG("realase device node %d", i);
        device_destroy(button_drv_class, MKDEV(major, i));
        if (g_btn_list[i].irq > 0) {
            free_irq(g_btn_list[i].irq, &g_btn_list[i]);
            DEBUG_LOG("realase irq %d", g_btn_list[i].irq);
            g_btn_list[i].irq = 0;
        }

        del_timer(&g_btn_list[i].timer);
    }

    class_destroy(button_drv_class);
	unregister_chrdev(major, "button_irq_drv");
    kfree(g_btn_list);
    g_btn_list = NULL;
    return 0;
}

static const struct of_device_id ask100_button_of_match[] = {
	{ .compatible = "100ask,button_dtb_drv", },
	{},
};

static struct platform_driver stm32mp157_button_driver = {
	.probe		= stm32mp157_button_probe,
	.remove		= stm32mp157_button_remove,
	.driver		= {
		.name	= "100ask_button",
        .of_match_table = ask100_button_of_match,
	},
};

module_platform_driver(stm32mp157_button_driver);