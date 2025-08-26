#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/errno.h>
#include <uapi/asm-generic/errno.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/kmod.h>
#include <asm/io.h>
#include <linux/usb/input.h>
#include <linux/hid.h>



MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kline <kline_code@example,com>");
MODULE_DESCRIPTION("Test for virtual usb mouse driver.");

#define DEBUG_LOG(fmt, ...) \
        printk(KERN_INFO "[%s line%d] %s(): "fmt"\n", \
            KBUILD_BASENAME, __LINE__, __FUNCTION__, ##__VA_ARGS__)

struct key_mouse_desc {
    struct usb_device *usbdev;
    struct usb_endpoint_descriptor *endpoint;
    struct input_dev *idev;
    struct urb *urb;
    dma_addr_t data_dma;
    signed char *data;
    int pipe;
    int maxp;
};

static struct key_mouse_desc *g_mouses = NULL;

static const struct usb_device_id usb_key_mouse_id_table[] = {
    { USB_INTERFACE_INFO(USB_INTERFACE_CLASS_HID, USB_INTERFACE_SUBCLASS_BOOT,
        USB_INTERFACE_PROTOCOL_MOUSE) },
    { }	/* Terminating entry */
};
MODULE_DEVICE_TABLE (usb, usb_key_mouse_id_table);


static void usb_key_mouse_irq(struct urb *urb)
{
    struct key_mouse_desc *mouse = urb->context;
    signed char *data = mouse->data;
    struct input_dev *dev = mouse->idev;
    int status = 0, i = 0;

    // 判断urb执行状态
    DEBUG_LOG("Enter with stauts: %d!", urb->status);
    switch (urb->status) {
        case 0:			/* success */
            break;
        case -ECONNRESET:	/* unlink */
        case -ENOENT:
        case -ESHUTDOWN:
            // DEBUG_LOG("Exception stauts: %d!", urb->status);
            return;
        /* -EPIPE:  should clear the halt */
        default:		/* error */
            goto resubmit;
    }

    // 打印urb中获取的数据
    for (i = 0; i < urb->actual_length; i++) {
        printk("byte[%d]: 0x%x ", i, data[i]);
    }

    // 上报input_even事件并同步
    input_report_key(dev, KEY_L, data[0] & 0x01);
    input_report_key(dev, KEY_S, data[0] & 0x02);
    input_report_key(dev, KEY_ENTER, data[0] & 0x04);
    input_sync(dev);

    // 提交下一个urb
resubmit:
    status = usb_submit_urb (urb, GFP_ATOMIC);
    if (status)
        DEBUG_LOG("can't resubmit intr, %s-%s/input0, status %d",
            mouse->usbdev->bus->bus_name,
            mouse->usbdev->devpath, status);

}

static int usb_key_mouse_open(struct input_dev *dev)
{
    struct key_mouse_desc *mouse = input_get_drvdata(dev);
    int err = 0;

    if (!mouse) {
        DEBUG_LOG("Invalid parameter!");
        return -EINVAL;
    }

    DEBUG_LOG("Enter!");
    // 分配urb usb_alloc_urb
    mouse->urb = usb_alloc_urb(0, GFP_KERNEL);
    if (!mouse->urb) {
        DEBUG_LOG("Alloc usb urb failed!");
        return -EINVAL;
    }

    // 填充urb usb_fill_int_urb
    usb_fill_int_urb(mouse->urb, mouse->usbdev, mouse->pipe, mouse->data,
            mouse->maxp,
            usb_key_mouse_irq, mouse, mouse->endpoint->bInterval);


    // 提交urb
    err = usb_submit_urb(mouse->urb, GFP_KERNEL);
    if (err) {
        DEBUG_LOG("submit urb failed!");
        return -EIO;
    }
    return 0;
}

static void usb_key_mouse_close(struct input_dev *dev)
{
    struct key_mouse_desc *mouse = input_get_drvdata(dev);

    if (!mouse) {
        DEBUG_LOG("Invalid parameter!");
        return;
    }

    DEBUG_LOG("Enter!");
    // 取消urb usb_kill_urb
    usb_kill_urb(mouse->urb);

    // 释放urb usb_free_urb
    usb_free_urb(mouse->urb);
}


static int usb_key_mouse_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
    struct usb_device *usbdev = interface_to_usbdev(intf);
    struct usb_host_interface *host_intf;
    struct usb_endpoint_descriptor *endpoint;
    struct input_dev *input_dev;
    int err = -EINVAL, pipe, maxp;

    DEBUG_LOG("Enter!");
    // 判断传入的usb_interface是否为中断输入端点
    host_intf = intf->cur_altsetting;
    if (host_intf->desc.bNumEndpoints != 1) {
        DEBUG_LOG("Invalid interface: muti-endpoint!");
        return -ENODEV;
    }
    endpoint = &host_intf->endpoint[0].desc;
    if (!usb_endpoint_is_int_in(endpoint)) {
        DEBUG_LOG("Invalid endpoint: not an interrupt input endpoint!");
        return -ENODEV;
    }
    pipe = usb_rcvintpipe(usbdev, endpoint->bEndpointAddress);
    maxp = usb_maxpacket(usbdev, pipe, usb_pipeout(pipe));

    // 分配鼠标描述结构体
    g_mouses = kzalloc(sizeof(*g_mouses), GFP_KERNEL);
    if (!g_mouses) {
        DEBUG_LOG("Alloc key_mouse_desc memory failed!");
        return -ENOMEM;
    }
    g_mouses->usbdev = usbdev;
    g_mouses->endpoint = endpoint;
    g_mouses->pipe = pipe;
    g_mouses->maxp = (maxp > 8 ? 8 : maxp);

    // 分配dma空间
    g_mouses->data = usb_alloc_coherent(usbdev, maxp, GFP_ATOMIC, &g_mouses->data_dma);
    if (!g_mouses->data) {
        DEBUG_LOG("Alloc key_mouse_desc DMA memory failed!");
        err = -ENOMEM;
        goto mouse_free;
    }

    // 分配input_event结构体
    input_dev = input_allocate_device();
    if (!input_dev) {
        DEBUG_LOG("Alloc input_dev memory failed!");
        err = -ENOMEM;
        goto dma_mem_free;
    }

    // 设置input_event信息：id、evbit、keybit、drvdata、open & close函数
    usb_to_input_id(usbdev, &input_dev->id);
    input_dev->dev.parent = &intf->dev;

    /* set 1: which type event ? */	
    __set_bit(EV_KEY, input_dev->evbit);

    /* set 2: which event ? */	
    __set_bit(KEY_L, input_dev->keybit);
    __set_bit(KEY_S, input_dev->keybit);
    __set_bit(KEY_ENTER, input_dev->keybit);
    g_mouses->idev = input_dev;

    input_dev->open = usb_key_mouse_open;
    input_dev->close = usb_key_mouse_close;
    input_set_drvdata(input_dev, g_mouses);

    // 注册input_event结构体
    err = input_register_device(g_mouses->idev);
    if (err) {
        DEBUG_LOG("Register input_dev failed!");
        err = -EINVAL;
        goto intput_dev_free;
    }
    usb_set_intfdata(intf, g_mouses);
    DEBUG_LOG("Probe success!");
    return 0;

intput_dev_free:
    input_free_device(input_dev);

dma_mem_free:
    usb_free_coherent(usbdev, 8, g_mouses->data, g_mouses->data_dma);

mouse_free:
    kfree(g_mouses);
    return err;
}

static void usb_key_mouse_disconnect(struct usb_interface *intf)
{
    struct key_mouse_desc *mouse = usb_get_intfdata(intf);

    DEBUG_LOG("Enter!");
    usb_set_intfdata(intf, NULL);

    // 反注册input_event结构体: input_unregister_device
    input_unregister_device(mouse->idev);

    // 释放dma空间
    usb_free_coherent(mouse->usbdev, mouse->maxp, mouse->data, mouse->data_dma);

    // 释放鼠标描述结构体
    kfree(mouse);
}

static struct usb_driver usb_key_mouse_driver = {
    .name		= "usbmouse_key",
    .probe		= usb_key_mouse_probe,
    .disconnect	= usb_key_mouse_disconnect,
    .id_table	= usb_key_mouse_id_table,
};

module_usb_driver(usb_key_mouse_driver);
