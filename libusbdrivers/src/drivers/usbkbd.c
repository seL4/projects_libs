/*
 * Copyright 2017, Data61
 * Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * ABN 41 687 119 230.
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(DATA61_BSD)
 */

#include "usbkbd.h"

#include <platsupport/chardev.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include "../services.h"

#define KBD_DEBUG
//#define KBDIRQ_DEBUG

#define KBD_ENABLE_IRQS

#define KBD_REPEAT_RATE_MS   200
#define KBD_REPEAT_DELAY_MS 1000

#ifdef KBD_DEBUG
#define KBD_DBG(...) _KBD_DBG(__VA_ARGS__)
#else
#define KBD_DBG(...) do{}while(0)
#endif

#ifdef KBDIRQ_DEBUG
#define KBDIRQ_DBG(...) _KBD_DBG(__VA_ARGS__)
#else
#define KBDIRQ_DBG(...) do{}while(0)
#endif

#define _KBD_DBG(k, ...)                                \
        do {                                            \
            usb_kbd_t _k = k;                           \
            if(_k && _k->udev){                         \
                printf("KBD %2d: ", _k->udev->addr);    \
            }else{                                      \
                printf("KBD  ?: ");                     \
            }                                           \
            printf(__VA_ARGS__);                        \
        }while(0)

enum kbd_protocol {
    BOOT = 0,
    REPORT = 1
};

enum kbd_bRequest {
    GET_REPORT   = 0x1,
    GET_IDLE     = 0x2,
    GET_PROTOCOL = 0x3,
    SET_REPORT   = 0x9,
    SET_IDLE     = 0xA,
    SET_PROTOCOL = 0xB
};

enum hid_ReportType {
    HID_INPUT    = 0x01,
    HID_OUTPUT   = 0x02,
    HID_FEATURE  = 0x03,
    HID_RESERVED = 0x04
};

static inline struct usbreq
__set_protocol_req(enum kbd_protocol p, int iface) {
    struct usbreq r = {
        .bmRequestType = (USB_DIR_IN | USB_TYPE_CLS | USB_RCPT_INTERFACE),
        .bRequest      = SET_PROTOCOL,
        .wValue        = p,
        .wIndex        = iface,
        .wLength       = 0
    };
    return r;
}

static inline struct usbreq
__set_idle_req(int idle_ms, int iface) {
    struct usbreq r = {
        .bmRequestType = (USB_DIR_OUT | USB_TYPE_CLS | USB_RCPT_INTERFACE),
        .bRequest      = SET_IDLE,
        .wValue        = idle_ms << 8,
        .wIndex        = iface,
        .wLength       = 0
    };
    return r;
}


static inline struct usbreq
__get_report(enum hid_ReportType type, int id, int iface, int len) {
    struct usbreq r = {
        .bmRequestType = (USB_DIR_IN | USB_TYPE_CLS | USB_RCPT_INTERFACE),
        .bRequest      = GET_REPORT,
        .wValue        = type << 8 | id,
        .wIndex        = iface,
        .wLength       = len
    };
    return r;
}

static inline struct usbreq
__set_report(enum hid_ReportType type, int id, int iface, int len) {
    struct usbreq r = {
        .bmRequestType = (USB_DIR_OUT | USB_TYPE_CLS | USB_RCPT_INTERFACE),
        .bRequest      = SET_REPORT,
        .wValue        = type << 8 | id,
        .wIndex        = iface,
        .wLength       = len
    };
    return r;
}

/*
 * Ring buffer for key logging
 */

struct ringbuf {
    char b[100];
    int head;
    int tail;
    int size;
};

static void
rb_init(struct ringbuf* rb)
{
    rb->head = 0;
    rb->tail = 0;
    rb->size = 0;
}

static int
rb_produce(struct ringbuf* rb, const char* str, int size)
{
    if (sizeof(rb->b) / sizeof(*rb->b) - rb->size >= size) {
        while (size--) {
            rb->b[rb->tail++] = *str++;
            if (rb->tail == sizeof(rb->b) / sizeof(*rb->b)) {
                rb->tail = 0;
            }
            rb->size++;
        }
        return 0;
    } else {
        return 1;
    }
}

static int
rb_consume(struct ringbuf* rb)
{
    char c;
    if (rb->size) {
        c = rb->b[rb->head++];
        if (rb->head == sizeof(rb->b) / sizeof(*rb->b)) {
            rb->head = 0;
        }
        rb->size--;
        return ((int)c) & 0xff;
    }
    return -1;
}

/*
 * Keyboard driver
 */

#define KBDRPT_RATE  ((KBD_REPEAT_RATE_MS + 3) / 4)
#define KBDRPT_DELAY ((KBD_REPEAT_DELAY_MS + 3) / 4)
#define KBD_PROTOCOL 1
#define KBD_KEYS_SIZE   8

#define KBDIND_CAPS    0x2
#define KBDIND_NUM     0x1
#define KBDIND_SCRL    0x4


struct usb_kbd_device {
/// A handle to the underlying USB device
    usb_dev_t udev;
/// Configuration parameters
    int ifno, cfgno, int_ep, int_max_pkt, int_rate_ms;
/// A generic transaction for indicator and idle requests
    struct xact xact[2];
    struct usbreq* req;
/// Indicator state. This is a pointer to our universal buffer at index 1
    uint8_t* ind;
/// An interrupt transaction
    struct xact int_xact[1];
/// Store old keys for repeat detection
    uint8_t old_keys[KBD_KEYS_SIZE];
/// new keys is a pointer to our interrupt buffer
    uint8_t* new_keys;
/// Cache the current repeat rate to avoid USB transfers
    int repeat_rate;
/// ring buffer for characters waiting to be read by the application.
    struct ringbuf rb;
};
typedef struct usb_kbd_device* usb_kbd_t;

struct udev_priv {
    struct usb_kbd_device kbd;
};

static inline usb_kbd_t
cdev_get_kbd(struct ps_chardevice* cdev)
{
    return (usb_kbd_t)cdev->vaddr;
}

static inline void
cdev_set_kbd(struct ps_chardevice* cdev, usb_kbd_t kbd)
{
    cdev->vaddr = (void*)kbd;
}

#define KBDFN_CTRL   0x01
#define KBDFN_SHIFT  0x02
#define KBDFN_ALT    0x04
#define KBDFN_WIN    0x08
#define KBDFN_LEFT(FN)     ((KBDFN_##FN) << 0)
#define KBDFN_RIGHT(FN)    ((KBDFN_##FN) << 4)
#define KBDFN_TEST(FN, x)  !!((x) & (KBDFN_LEFT(FN) | KBDFN_RIGHT(FN)))
static const char std_kcodes[] = {
    /*       0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F */
    /*0x00*/                'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l',
    /*0x10*/'m', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', '1', '2',
    /*0x20*/'3', '4', '5', '6', '7', '8', '9', '0', 10, 27,  8,  9, ' ', '-', '=', '[',
    /*0x30*/']', 92, 0 , ';', 39, '`', ',', '.', '/'
};

static const char stdshift_kcodes[] = {
    /*       0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F */
    /*0x00*/                'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L',
    /*0x10*/'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', '!', '@',
    /*0x20*/'#', '$', '%', '^', '&', '*', '(', ')', 10, 27,  8,  9, ' ', '_', '+', '{',
    /*0x30*/'}', '|', 0 , ':', 39, '~', '<', '>', '?'
};


static const char num_kcodes[] = {
    /*       0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F */
    /*0x50*/                '/', '*', '-', '+', 10, '1', '2', '3', '4', '5', '6', '7',
    /*0x60*/'8', '9', '0', '.'
};

#define KBDKEY_NONE       0x00
#define KBDKEY_MASH       0x01
#define KBDKEY_NUMLOCK    0x53
#define KBDKEY_CAPSLOCK   0x39
#define KBDKEY_SCROLLLOCK 0x47

static int
kbd_update_ind(usb_kbd_t kbd)
{
    /* Send the request! */
    *kbd->req = __set_report(HID_OUTPUT, 0, kbd->ifno, 1);
    return usbdev_schedule_xact(kbd->udev, kbd->udev->ep_ctrl, kbd->xact, 2,
                                NULL, NULL);
}

static int
kbd_update_repeat_rate(usb_kbd_t kbd)
{
    KBDIRQ_DBG(kbd, "Changing rate to %dms\n", kbd->repeat_rate * 4);
    *kbd->req = __set_idle_req(kbd->repeat_rate, kbd->ifno);
    return usbdev_schedule_xact(kbd->udev, kbd->udev->ep_ctrl,
                                kbd->xact, 1, NULL, NULL);
}

static int
kbd_irq_handler(void* token, enum usb_xact_status stat, int bytes_remaining)
{
    usb_kbd_t kbd = (usb_kbd_t)token;
    uint8_t afn;
    uint8_t key;
    int new_rate = -1;
    char c;
    int len;

    /* Check the status */
    if (stat != XACTSTAT_SUCCESS) {
        KBD_DBG(kbd, "Received unsuccessful IRQ\n");
        return 1;
    }
    len = kbd->int_xact->len - bytes_remaining;
    if (len < 4) {
        KBD_DBG(kbd, "Short read on INT packet (%d)\n", len);
        return 1;
    }
#if defined(KBDIRQ_DEBUG)
    {
        int i;
        for (i = 0; i < len; i++) {
            printf("[0x%02x]", kbd->new_keys[i]);
        }
        printf("\n");
    }
#endif

    /* Multiple key press. Ignore input */
    if (kbd->new_keys[3] != KBDKEY_NONE) {
        kbd->new_keys[2] = kbd->old_keys[2] = KBDKEY_NONE;
    }
    /* Read in key parameters */
    afn = kbd->new_keys[0];
    key = kbd->new_keys[2];
    /* Handle repeat delay */
    if (key == KBDKEY_NONE) {
        /* No key pressed or someone is being a jerk - idle */
        new_rate = 0;
    } else if (kbd->old_keys[2] == key) {
        /* Someone is holding down a key */
        new_rate = KBDRPT_RATE;
    } else {
        /* Someone pressed a new key! Start repeat delay */
        new_rate = KBDRPT_DELAY;
    }
    /* Adjust the idle delay if required */
    if (new_rate != kbd->repeat_rate) {
        kbd->repeat_rate = new_rate;
        kbd_update_repeat_rate(kbd);
    }
    /* Process the key */
    memcpy(kbd->old_keys, kbd->new_keys, KBD_KEYS_SIZE);
    if (key == KBDKEY_NONE || key == KBDKEY_MASH) {
        /* Ignore it */
    } else if (key < 0x04) {
        printf("<!0x%x>", key);
    } else if (key < 0x39) {
        char cl;
        /* Decode the key */
        c = std_kcodes[key - 0x04];
        if (KBDFN_TEST(CTRL, afn) && c >= '@' && c <= '_') {
            c -= '@';
        } else if (KBDFN_TEST(SHIFT, afn)) {
            c = stdshift_kcodes[key - 0x04];
        }
        /* Check and update for capslock */
        cl = c | 0x20;
        if (cl >= 'a' && cl <= 'z' && (*kbd->ind & KBDIND_CAPS)) {
            c ^= 0x20;
        }
        /* Register the character */
        if (KBDFN_TEST(ALT, afn)) {
            char str[2] = {0x1B, c};
            rb_produce(&kbd->rb, str, 2);
        } else {
            rb_produce(&kbd->rb, &c, 1);
        }
    } else if (key == KBDKEY_NUMLOCK) {
        *kbd->ind ^= KBDIND_NUM;
        kbd_update_ind(kbd);
    } else if (key == KBDKEY_CAPSLOCK) {
        *kbd->ind ^= KBDIND_CAPS;
        kbd_update_ind(kbd);
    } else if (key == KBDKEY_SCROLLLOCK) {
        *kbd->ind ^= KBDIND_SCRL;
        kbd_update_ind(kbd);
    } else if (key < 0x54) {
        /* TODO handle these codes (see below) */
        printf("<!0x%x>", key);
    } else if (key < 0x64 && (*kbd->ind & KBDIND_NUM)) {
        c = num_kcodes[key - 0x54];
        rb_produce(&kbd->rb, &c, 1);
    } else if (key < 0x66) {
        /* TODO find scan codes for these keys and keypad
         * with no numlock */
#if 0
        F1 - F12         3a - 45
        prntscrn / sysrq 46
        pause / break    48
        insert         49
        home           4a
        pgup           4b
        delete         4c
        end            4d
        pgdwn          4e
        right          4f
        left           50
        down           51
        up             52
        macro          64
        dropdown       65
#endif
        printf("<!0x%x>", key);
    } else {
        printf("<!!0x%x>", key);
    }
    return 1;
}

static ssize_t
kbd_read(ps_chardevice_t* d, void* vdata, size_t bytes,
         chardev_callback_t cb, void* token)
{
    usb_kbd_t kbd;
    char *data;
    int i;
    kbd = cdev_get_kbd(d);
    data = (char*)vdata;
    for (i = 0; i < bytes; i++) {
        int c;
        c = rb_consume(&kbd->rb);
        if (c >= 0) {
            *data++ = c;
        } else {
            break;
        }
    }
    return i;
}

static int
kbd_connect(usb_dev_t udev)
{
    usb_kbd_t kbd;
    struct xact xact[4];
    struct usbreq* req;
    int i;
    int err;

    assert(udev);
    kbd = &udev->dev_data->kbd;
    assert(kbd);
    /* Create a buffer for setting up the device */
    for (i = 0; i < sizeof(xact) / sizeof(*xact); i++) {
        xact[i].type = PID_SETUP;
        xact[i].len = sizeof(*req);
    }
    err = usb_alloc_xact(kbd->udev->dman, xact, sizeof(xact) / sizeof(*xact));
    if (err) {
        assert(0);
        return -1;
    }
    KBD_DBG(kbd, "Configuring keyboard (config=%d, iface=%d)\n",
            kbd->cfgno, kbd->ifno);
    req = xact_get_vaddr(&xact[0]);
    *req = __set_configuration_req(kbd->cfgno);
    req = xact_get_vaddr(&xact[1]);
    *req = __set_interface_req(kbd->ifno);
    req = xact_get_vaddr(&xact[2]);
    *req = __set_protocol_req(BOOT, kbd->ifno);
    req = xact_get_vaddr(&xact[3]);
    *req = __set_idle_req(0, kbd->ifno);
    err = usbdev_schedule_xact(udev, kbd->udev->ep_ctrl,  xact,
                               sizeof(xact) / sizeof(*xact), NULL, NULL);
    kbd->repeat_rate = 0;
    usb_destroy_xact(udev->dman, xact, sizeof(xact) / sizeof(*xact));
    if (err < 0) {
        KBD_DBG(kbd, "Keyboard initialisation error\n");
        assert(err >= 0);
        return -1;
    }
    KBD_DBG(kbd, "Keyboard configured\n");

    /* Initialise LEDS */
    kbd_update_ind(kbd);
    /* Initialise IRQs */
#if defined(KBD_ENABLE_IRQS)
    KBD_DBG(kbd, "Scheduling IRQS\n");
    /* Register for interrupts */
    /* FIXME: Search for the right ep */
    usbdev_schedule_xact(udev, udev->ep[0], kbd->int_xact, 1,
                         &kbd_irq_handler, kbd);
#else
    (void)kbd_irq_handler;
#endif
    KBD_DBG(kbd, "Successfully initialised\n");
    return 0;
}

static int
kbd_disconnect(usb_dev_t udev)
{
    /* Nothing to do... We are a passive device */
    usb_kbd_t kbd = &udev->dev_data->kbd;
    KBD_DBG(kbd, "Disconnect requested\n");
    return 0;
}


static int
kbd_config_cb(void* token, int cfg, int iface,
              struct anon_desc* d)
{
    usb_kbd_t kbd = (usb_kbd_t)token;
    assert(kbd);
    if (d && kbd->int_ep == -1) {
        switch (d->bDescriptorType) {
        case INTERFACE: {
            struct iface_desc *id = (struct iface_desc*)d;
            if (id->bInterfaceClass == USB_CLASS_HID &&
                    id->bInterfaceProtocol == KBD_PROTOCOL) {
                kbd->ifno = iface;
                kbd->cfgno = cfg;
            }
            break;
        }
        case ENDPOINT: {
            struct endpoint_desc *e = (struct endpoint_desc*)d;
            /* We just take the first endpoint */
            if (kbd->ifno != -1) {
                kbd->int_ep = e->bEndpointAddress & 0xf;
                kbd->int_max_pkt = e->wMaxPacketSize;
                kbd->int_rate_ms = e->bInterval * 2;
            }
            break;
        }
        default:
            /* Don't care */
            break;
        }
    }
    return 0;
}

int
usb_kbd_driver_bind(usb_dev_t udev, struct ps_chardevice *cdev)
{
    usb_kbd_t kbd = NULL;
    int class;
    struct usbreq *req;
    struct udev_priv* dev_data;
    int err;
    assert(udev);
    /* Check that this is a HID device */
    class = usbdev_get_class(udev);
    if (class != USB_CLASS_HID && class != USB_CLASS_UNSPECIFIED) {
        KBD_DBG(kbd, "No keyboard candidate at address %d\n", udev->addr);
        return -1;
    }

    KBD_DBG(kbd, "Found a keyboard candidate\n");
    /* Create a keyboard device driver structure */
    dev_data = usb_malloc(sizeof(*dev_data));
    if (dev_data == NULL) {
        KBD_DBG(kbd, "No heap memory for driver\n");
        assert(0);
        return -1;
    }
    kbd = &dev_data->kbd;
    kbd->udev = udev;
    kbd->cfgno = kbd->ifno = kbd->int_ep = -1;
    rb_init(&kbd->rb);

    /* Okay, now we *might be a keyboard... Read the config */
    err = usbdev_parse_config(udev, kbd_config_cb, (void*)kbd);
    if (err || kbd->int_ep == -1) {
        KBD_DBG(kbd, "Not a USB keyboard\n");
        usb_free(dev_data);
        return -1;
    }
    KBD_DBG(kbd, "Found USB keyboard device\n");
    udev->dev_data = dev_data;
    /* Allocate a buffer for our IRQs */
    kbd->int_xact[0].len = KBD_KEYS_SIZE;
    kbd->int_xact[0].type = PID_IN;
    err = usb_alloc_xact(udev->dman, kbd->int_xact, 1);
    if (err) {
        assert(0);
        usb_free(dev_data);
        return -1;
    }
    kbd->new_keys = xact_get_vaddr(&kbd->int_xact[0]);
    memset(kbd->old_keys, 0, sizeof(kbd->old_keys));
    /* Allocate a buffer for indicators/repeat delay */
    kbd->xact[0].len = sizeof(*req);
    kbd->xact[0].type = PID_SETUP;
    kbd->xact[1].len = 1;
    kbd->xact[1].type = PID_OUT;
    err = usb_alloc_xact(udev->dman, kbd->xact, 2);
    if (err) {
        usb_destroy_xact(udev->dman, kbd->int_xact, 1);
        usb_free(dev_data);
        assert(0);
        return -1;
    }
    kbd->req = xact_get_vaddr(&kbd->xact[0]);
    kbd->ind = xact_get_vaddr(&kbd->xact[1]);
    *kbd->ind = KBDIND_NUM;
    /* Configure and set up the device */
    udev->connect = &kbd_connect;
    udev->disconnect = &kbd_disconnect;
    err = udev->connect(udev);
    if (err) {
        assert(0);
        return -1;
    }
    /* Bind to a character device */
    if (ps_cdev_new(NULL, cdev) == NULL) {
        usb_destroy_xact(udev->dman, kbd->int_xact, 1);
        usb_destroy_xact(udev->dman, kbd->xact, 1);
        usb_free(dev_data);
        assert(0);
        return -2;
    }
    cdev_set_kbd(cdev, kbd);
    cdev->read = &kbd_read;
    /* DONE! */
    return 0;
}


