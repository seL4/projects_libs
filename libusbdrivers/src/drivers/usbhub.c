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

#include "usbhub.h"

#include <stdio.h>
#include <string.h>

#include <assert.h>

#include <utils/util.h>

#include "../services.h"

#define HUBINT_RATE_MS 100

#define HUB_ENABLE_IRQS

#define HUB_DEBUG
//#define HUBEM_DEBUG

#ifdef HUB_DEBUG
#define dprintf(...) printf(__VA_ARGS__)
#else
#define dprintf(...) do{}while(0)
#endif

#define HUB_DBG(h, ...)                                 \
        do {                                            \
            usb_hub_t hub = h;                          \
            if(hub){                                    \
                dprintf("HUB %2d: ", hub->udev->addr);  \
            }else{                                      \
                dprintf("HUB  ?: ");                    \
            }                                           \
            dprintf(__VA_ARGS__);                       \
        }while(0)

#ifdef HUBEM_DEBUG
#define DHUBEM(...) printf("HUBEM   :" __VA_ARGS__)
#else
#define DHUBEM(...) do{}while(0)
#endif



/*** USB spec chapter 11 page 262 ***/

struct hub_desc {
/// Number of bytes in this descriptor, including this byte.
    uint8_t bDescLength;
/// Descriptor Type, value: 0x29 for Hub Descriptor.
#define DESCRIPTOR_TYPE_HUB 0x29
    uint8_t bDescriptorType;
/// Number of downstream ports that this hub supports.
    uint8_t bNbrPorts;
/// Hub characteristics.
#define HUBCHAR_POWER_SWITCH_SINGLE BIT(0)
#define HUBCHAR_POWER_SWITCH_NONE   BIT(1)
#define HUBCHAR_COMPOUND_DEVICE     BIT(2)
#define HUBCHAR_OVER_CURRENT_SINGLE BIT(3)
#define HUBCHAR_OVER_CURRENT_NONE   BIT(4)
    uint16_t wHubCharacteristics;
/// Time (in 2ms intervals) until power is stable after child port power on
    uint8_t bPwrOn2PwrGood;
/// Maximum current requirements of the hub controller
    uint8_t bHubContrCurrent;
    /* The size of the remaining fields depends on the number of ports
     * Bit x will correspond to port x
     * DeviceRemovable bitmap: 1-not removable.
     * PortPwrCtrlMask bitmap: 1-requires manual power on (not ganged).
     */
    uint8_t portcfg[64];
} __attribute__ ((packed));

/****************
 *** FEATURES ***
 ****************/
static inline struct usbreq
__clear_port_feature_req(uint16_t port, uint16_t feature) {
    struct usbreq r = {
        .bmRequestType = 0b00100011,
        .bRequest      = CLR_FEATURE,
        .wValue        = feature,
        .wIndex        = port,
        .wLength       = 0
    };
    return r;
}

static inline struct usbreq
__clear_hub_feature_req(uint16_t feature) {
    struct usbreq r = {
        .bmRequestType = 0b00100000,
        .bRequest      = CLR_FEATURE,
        .wValue        = feature,
        .wIndex        = 0,
        .wLength       = 0
    };
    return r;
}

static inline struct usbreq
__set_port_feature_req(uint16_t port, uint16_t feature) {
    struct usbreq r = {
        .bmRequestType = 0b00100011,
        .bRequest      = SET_FEATURE,
        .wValue        = feature,
        .wIndex        = port,
        .wLength       = 0
    };
    return r;
}

static inline struct usbreq
__set_hub_feature_req(uint16_t feature) {
    struct usbreq r = {
        .bmRequestType = 0b00100000,
        .bRequest      = SET_FEATURE,
        .wValue        = feature,
        .wIndex        = 0,
        .wLength       = 0
    };
    return r;
}


static inline struct usbreq
__get_port_status_req(uint16_t port) {
    struct usbreq r = {
        .bmRequestType = 0b10100011,
        .bRequest      = GET_STATUS,
        .wValue        = 0,
        .wIndex        = port,
        .wLength       = 4
    };
    return r;
}


static inline struct usbreq
__get_hub_status_req(void) {
    struct usbreq r = {
        .bmRequestType = 0b10100000,
        .bRequest      = GET_STATUS,
        .wValue        = 0,
        .wIndex        = 0,
        .wLength       = 4
    };
    return r;
}

static inline struct usbreq
__get_hub_descriptor_req(void) {
    struct usbreq r = {
        .bmRequestType = 0b10100000,
        .bRequest      = GET_DESCRIPTOR,
        .wValue        = DESCRIPTOR_TYPE_HUB << 8,
        .wIndex        = 0,
        .wLength       = sizeof(struct hub_desc)
    };
    return r;
}

static inline struct usbreq
__set_hub_descriptor_req(void) {
    struct usbreq r = {
        .bmRequestType = 0b00100000,
        .bRequest      = SET_DESCRIPTOR,
        .wValue        = DESCRIPTOR_TYPE_HUB << 8,
        .wIndex        = 0,
        .wLength       = sizeof(struct hub_desc)
    };
    return r;
}


struct usb_hub_port {
    struct usb_dev* udev;
};

struct usb_hub {
    usb_dev_t udev;
/// Configuration parameters
    struct endpoint *ep_int;
    int ifno, cfgno, int_ep, int_max_pkt, int_rate_ms;
/// Port book keeping
    int nports;
    struct usb_hub_port* port;
    int power_good_delay_ms;
/// IRQs
    struct xact int_xact;
    uint8_t* intbm;
};

struct usb_hubem {
    int hubem_nports;
    int pwr_delay_ms;
    int (*set_pf)(void *token, int port,
                  enum port_feature feature);
    int (*clr_pf)(void *token, int port,
                  enum port_feature feature);
    int (*get_pstat)(void* token, int port,
                     struct port_status* ps);
    void* token;
};


static void
_handle_port_change(usb_hub_t h, int port)
{
    struct xact xact[2];
    struct usbreq *req;
    struct port_status* sts;
    uint16_t status, change;
    int ret;
    assert(sizeof(unsigned short) == 2);

    HUB_DBG(h, "Handle status change port %d\n", port);
    /* Setup packet */
    xact[0].type = PID_SETUP;
    xact[0].len = sizeof(*req);
    xact[1].type = PID_IN;
    xact[1].len = sizeof(*sts);

    /* FIXME: Memory leak, no usb_destroy_xact in this function */
    ret = usb_alloc_xact(h->udev->dman, xact, 2);
    assert(!ret);
    req = xact_get_vaddr(&xact[0]);
    sts = xact_get_vaddr(&xact[1]);


    /* get the associated ports status change */
    if (port == 0) {
        *req = __get_hub_status_req();
    } else {
        *req = __get_port_status_req(port);
    }
    HUB_DBG(h, "Getting status for port %d\n", port);
    ret = usbdev_schedule_xact(h->udev, h->udev->ep_ctrl, xact, 2, NULL, NULL);
    assert(ret >= 0);
    change = sts->wPortChange;
    status = sts->wPortStatus;

    if (port == 0) {
        /* HUB */
        assert(0);
    } else {
        HUB_DBG(h, "Status change (0x%x) on port %d:\n",
                change, port);
        if (change & BIT(PORT_CONNECTION)) {
            change &= ~BIT(PORT_CONNECTION);
            /* Clear the change flag */
            *req = __clear_port_feature_req(port,
                                            C_PORT_CONNECTION);
            ret = usbdev_schedule_xact(h->udev, h->udev->ep_ctrl,
                                       xact, 1, NULL, NULL);
            assert(ret >= 0);
            if (status & BIT(PORT_CONNECTION)) {
                HUB_DBG(h, "port %d connected\n", port);
                /* USB spec 9.1.2 */
                msdelay(100);
                assert(ret >= 0);
                /* reset the port to enable it */
                HUB_DBG(h, "Resetting port %d\n", port);
                *req = __set_port_feature_req(port, PORT_RESET);
                ret = usbdev_schedule_xact(h->udev, h->udev->ep_ctrl,
                                           xact, 1, NULL, NULL);
                assert(ret >= 0);
                if (!ret) {
                    /* Reset changes the status so call again */
                    return _handle_port_change(h, port);
                }
            } else {
                /* Disconnect */
                HUB_DBG(h, "port %d disconnected\n", port);
                usbdev_disconnect(h->port[port - 1].udev);
                h->port[port - 1].udev = NULL;
            }
        }
        if (change & BIT(PORT_ENABLE)) {
            HUB_DBG(h, "%sabled\n", (status & BIT(PORT_ENABLE)) ?
                    "en" : "dis");
            change &= ~PORT_ENABLE;
            *req = __clear_port_feature_req(port,
                                            C_PORT_ENABLE);
            ret = usbdev_schedule_xact(h->udev, h->udev->ep_ctrl,
                                       xact, 1, NULL, NULL);
            assert(ret >= 0);
        }
        if (change & BIT(PORT_SUSPEND)) {
            HUB_DBG(h, "%s\n", (status & BIT(PORT_SUSPEND)) ?
                    "suspended" : "resumed");
            change &= ~BIT(PORT_SUSPEND);
            *req = __clear_port_feature_req(port,
                                            C_PORT_SUSPEND);
            ret = usbdev_schedule_xact(h->udev, h->udev->ep_ctrl,
                                       xact, 1, NULL, NULL);
            assert(ret >= 0);
        }
        if (change & BIT(PORT_OVER_CURRENT)) {
            HUB_DBG(h, "over current %s\n",
                    (status & BIT(PORT_OVER_CURRENT)) ?
                    "detected" : "removed");
            change &= ~BIT(PORT_OVER_CURRENT);
            *req = __clear_port_feature_req(port,
                                            C_PORT_OVER_CURRENT);
            ret = usbdev_schedule_xact(h->udev, h->udev->ep_ctrl,
                                       xact, 1, NULL, NULL);
            assert(ret >= 0);
        }
        if (change & BIT(PORT_RESET)) {
            HUB_DBG(h, "reset %s\n", (status & BIT(PORT_RESET)) ?
                    "" : "released");
            change &= ~BIT(PORT_RESET);
            *req = __clear_port_feature_req(port, C_PORT_RESET);
            ret = usbdev_schedule_xact(h->udev, h->udev->ep_ctrl,
                                       xact, 1, NULL, NULL);
            assert(ret >= 0);
            /* Was the reset part of the init process? */
            if (status & BIT(PORT_CONNECTION)) {
                enum usb_speed speed;
                usb_dev_t new_dev;
                usb_hub_t new_hub;
                assert(status & BIT(PORT_ENABLE));

                /* Create the new device */
                if (status & BIT(PORT_HIGH_SPEED)) {
                    speed = USBSPEED_HIGH;
                } else if (status & BIT(PORT_LOW_SPEED)) {
                    speed = USBSPEED_LOW;
                } else {
                    speed = USBSPEED_FULL;
                }
                ret = usb_new_device(h->udev, port, speed, &new_dev);
                if (ret) {
                    HUB_DBG(h, "Failed to initialise new device"
                            " on port %d\n", port);
                    HUB_DBG(h, "Resetting port %d\n", port);
                    msdelay(10);
                    *req = __set_port_feature_req(port, PORT_RESET);
                    ret = usbdev_schedule_xact(h->udev, h->udev->ep_ctrl,
                                               xact, 1, NULL, NULL);
                    assert(ret >= 0);
                    printf("Scheduled!\n");
                    return _handle_port_change(h, port);
                } else {
                    h->port[port - 1].udev = new_dev;
                    /* See if we can bind a hub */
                    usb_hub_driver_bind(new_dev, &new_hub);
                }
		return;
            }
        }
        if (change) {
            HUB_DBG(h, "Unhandled port change 0x%x\n", change);
        }
        assert(ret >= 0);

	/*
	 * FIXME: A hack to force serializing device enumeration, this function
	 * needs rewrite.
	 */
	_handle_port_change(h, port);
    }
}

static int
hub_irq_handler(void* token, enum usb_xact_status stat, int bytes_remaining)
{
    usb_hub_t h = (usb_hub_t)token;
    int i, j;
    int handled = 0;
    uint8_t* intbm;
    int len = h->int_xact.len - bytes_remaining;

    /* Check the status */
    if (stat != XACTSTAT_SUCCESS) {
        HUB_DBG(h, "Received unsuccessful IRQ\n");
        return 1;
    }

    HUB_DBG(h, "Handling IRQ\n");

    intbm = h->intbm;
    assert(intbm == xact_get_vaddr(&h->int_xact));
    for (i = 0; i < len; i++) {
        /* Check if any bits have changed */
        if (intbm[i] == 0) {
            continue;
        }
        /* Scan bitfield */
        for (j = 0; j < 8; j++) {
            if ((1 << j) & intbm[i]) {
                int port = i * 8 + j;
                _handle_port_change(h, port);
                handled++;
            }
        }
        intbm[i] = 0;
    }
    if (!handled) {
        HUB_DBG(h, "Spurious IRQ\n");
    }

    usbdev_schedule_xact(h->udev, h->udev->ep[0],
                         &h->int_xact, 1, &hub_irq_handler, h);
    return 0;
}

static int
hub_config_cb(void* token, int cfg, int iface,
              struct anon_desc* d)
{
    usb_hub_t hub = (usb_hub_t)token;
    assert(hub);
    if (d) {
        switch (d->bDescriptorType) {
        case ENDPOINT: {
            struct endpoint_desc *e = (struct endpoint_desc*)d;
            /* We just take the first endpoint */
            hub->int_ep = e->bEndpointAddress & 0xf;
            hub->int_max_pkt = e->wMaxPacketSize;
            hub->int_rate_ms = e->bInterval * 2;
            hub->ifno = iface;
            hub->cfgno = cfg;
            break;
        }
        default:
            /* Don't care */
            break;
        }
        return 0;
    } else {
        return 0;
    }
}

int
usb_hub_driver_bind(usb_dev_t udev, usb_hub_t* hub)
{
    usb_hub_t h;
    struct usbreq *req;
    struct hub_desc* hdesc;
    struct xact xact[2];
    int err;
    int i;

    /* Check the class */
    if (usbdev_get_class(udev) != USB_CLASS_HUB) {
        return -1;
    }

    /* Allocate memory */
    h = (usb_hub_t)usb_malloc(sizeof(*h));
    if (h == NULL) {
        assert(0);
        return -2;
    }
    memset(h, 0, sizeof(*h));
    h->udev = udev;
    /* Get hub descriptor for nports and power delay */
    HUB_DBG(h, "Get hub descriptor\n");
    xact[0].type = PID_SETUP;
    xact[0].len = sizeof(*req);
    xact[1].type = PID_IN;
    xact[1].len = sizeof(*hdesc);
    err = usb_alloc_xact(udev->dman, xact, 2);
    assert(!err);
    req = xact_get_vaddr(&xact[0]);
    *req = __get_hub_descriptor_req();
    err = usbdev_schedule_xact(udev, h->udev->ep_ctrl, xact, 2, NULL, NULL);
    if (err < 0) {
        assert(0);
        return -1;
    }
    hdesc = xact_get_vaddr(&xact[1]);
    h->nports = hdesc->bNbrPorts;
    h->power_good_delay_ms = hdesc->bPwrOn2PwrGood * 2;
    usb_destroy_xact(udev->dman, xact, 2);
    h->port = (struct usb_hub_port*)usb_malloc(
                  sizeof(*h->port) * h->nports);
    memset(h->port, 0, sizeof(*h->port) * h->nports);
    HUB_DBG(h, "Parsing config\n");
    h->int_ep = -1;
    err = usbdev_parse_config(h->udev, &hub_config_cb, h);
    if (err || h->int_ep == -1) {
        assert(0);
        return -1;
    }
    HUB_DBG(h, "Configure HUB\n");
    xact[0].type = PID_SETUP;
    xact[0].len = sizeof(*req);

    err = usb_alloc_xact(h->udev->dman, xact, 1);
    if (err) {
        assert(!err);
        return -1;
    }
    req = xact_get_vaddr(&xact[0]);
    *req = __set_configuration_req(h->cfgno);

    err = usbdev_schedule_xact(udev, h->udev->ep_ctrl, xact, 1, NULL, NULL);
    if (err < 0) {
        assert(err >= 0);
        return -1;
    }
    usb_destroy_xact(udev->dman, xact, 1);

    /* Power up ports */
    xact[0].type = PID_SETUP;
    xact[0].len = sizeof(*req);

    usb_alloc_xact(h->udev->dman, xact, 1);
    req = xact_get_vaddr(&xact[0]);
    for (i = 1; i <= h->nports; i++) {
        HUB_DBG(h, "Power on port %d\n", i);
        *req = __set_port_feature_req(i, PORT_POWER);
        err = usbdev_schedule_xact(h->udev, h->udev->ep_ctrl,
			xact, 1, NULL, NULL);
        assert(err >= 0);
    }
    msdelay(h->power_good_delay_ms);
    usb_destroy_xact(udev->dman, xact, 1);
#if !defined(HUB_ENABLE_IRQS)
    /* Setup ports */
    for (i = 1; i <= h->nports; i++) {
        _handle_port_change(h, i);
    }
#endif
#if defined(HUB_ENABLE_IRQS)
    h->int_xact.type = PID_IN;
    /*
     * USB 2.0 spec[11.12.4] says the packet size should be (nport + 7)/8, but
     * some hubs are known to send more data, which would cause a "babble". So
     * we use maximum packet size instead, short packet does no harm.
     */
    h->int_xact.len = h->int_max_pkt;
    err = usb_alloc_xact(udev->dman, &h->int_xact, 1);
    assert(!err);
    h->intbm = xact_get_vaddr(&h->int_xact);
    HUB_DBG(h, "Registering for INT\n");
    /* FIXME: Search for the right ep */
    usbdev_schedule_xact(udev, udev->ep[0],
                         &h->int_xact, 1, &hub_irq_handler, h);
#else
    h->intbm = NULL;
    h->int_xact.vaddr = NULL;
    h->int_xact.paddr = 0;
    h->int_xact.len = 0;
    (void)hub_irq_handler;
#endif
    *hub = h;

    return 0;
}


/*********************
 *** Hub emulation ***
 *********************/

static struct device_desc _hub_device_desc = {
    .bLength = sizeof(struct device_desc),
    .bDescriptorType = DEVICE,
    .bcdUSB = 0x200,
    .bDeviceClass = USB_CLASS_HUB,
    .bDeviceSubClass = 0,
    .bDeviceProtocol = 2,
    .bMaxPacketSize0  = 64,
    .idVendor = 0xFEED,
    .idProduct = 0xBEEF,
    .bcdDevice = 1234,
    .iManufacturer = 0,
    .iProduct = 0,
    .iSerialNumber = 0,
    .bNumConfigurations = 1
};

static struct iface_desc _hub_iface_desc = {
    .bLength = sizeof(_hub_iface_desc),
    .bDescriptorType = INTERFACE,
    .bInterfaceNumber = 0,
    .bAlternateSetting = 0,
    .bNumEndpoints = 1,
    .bInterfaceClass = 9,
    .bInterfaceSubClass = 0,
    .bInterfaceProtocol = 1,
    .iInterface = 0
};

static struct endpoint_desc _hub_endpoint_desc = {
    .bLength = sizeof(_hub_endpoint_desc),
    .bDescriptorType = ENDPOINT,
    .bEndpointAddress = 0x81,
    .bmAttributes = 0x3,
    .wMaxPacketSize = 0x1,
    .bInterval = 0xc
};

static struct config_desc _hub_config_desc = {
    .bLength = sizeof(_hub_config_desc),
    .bDescriptorType = CONFIGURATION,
    .wTotalLength = sizeof(_hub_config_desc) +
    sizeof(_hub_iface_desc)  +
    sizeof(_hub_endpoint_desc),
    .bNumInterfaces = 1,
    .bConfigurationValue = 1,
    .iConfigurationIndex = 0,
    .bmAttributes = (1 << 7),
    .bMaxPower = 100/*mA*/ / 2
};

static struct hub_desc _hub_hub_desc = {
    .bDescLength = 0x8,
    .bDescriptorType = DESCRIPTOR_TYPE_HUB,
    .bNbrPorts = 2,
    .wHubCharacteristics = 0,
    .bPwrOn2PwrGood = 0xff,
    .bHubContrCurrent = 0,
    .portcfg = {0}
};

static int
hubem_get_descriptor(usb_hubem_t dev, struct usbreq* req, void* buf, int len)
{
    int act_len = 0;
    int dtype = req->wValue >> 8;
    switch (dtype) {
    case DEVICE: {
        struct device_desc* ret = (struct device_desc*)buf;
        DHUBEM("Get device descriptor\n");
        act_len = MIN(len, sizeof(*ret));
        memcpy(ret, &_hub_device_desc, act_len);
        return act_len;
    }
    case DESCRIPTOR_TYPE_HUB: {
        struct hub_desc* ret = (struct hub_desc*)buf;
        int nregs = (dev->hubem_nports + 7) / 8;
        int i;
        DHUBEM("Get hub type descriptor\n");
        _hub_hub_desc.bNbrPorts = dev->hubem_nports;
        _hub_hub_desc.bPwrOn2PwrGood = dev->pwr_delay_ms / 2;
        _hub_hub_desc.bDescLength = 7 + nregs * 2;
        for (i = 0; i < nregs; i++) {
            _hub_hub_desc.portcfg[i] = 0;
            _hub_hub_desc.portcfg[i + nregs] = 0;
        }
        act_len = MIN(_hub_hub_desc.bDescLength, len);
        memcpy(ret, &_hub_hub_desc, act_len);
        return act_len;
    }
    case CONFIGURATION: {
        int cp_len;
        int pos = 0;
        int act_len;
        DHUBEM("Get configuration descriptor\n");
        act_len = MIN(_hub_config_desc.wTotalLength, len);
        /* Copy the config */
        cp_len = MIN(act_len - pos, _hub_config_desc.bLength);
        memcpy(buf + pos, &_hub_config_desc, cp_len);
        pos += cp_len;
        /* Copy the iface */
        cp_len = MIN(act_len - pos, _hub_iface_desc.bLength);
        memcpy(buf + pos, &_hub_iface_desc, cp_len);
        pos += cp_len;
        /* copy the endpoint */
        _hub_endpoint_desc.wMaxPacketSize = (dev->hubem_nports + 7) / 8;
        cp_len = MIN(act_len - pos, _hub_endpoint_desc.bLength);
        memcpy(buf + pos, &_hub_endpoint_desc, cp_len);
        pos += cp_len;
        assert(pos = act_len);
        return act_len;
    }
    case INTERFACE: {
        int act_len;
        DHUBEM("Get interface descriptor\n");
        act_len = MIN(_hub_iface_desc.bLength, len);
        memcpy(buf, &_hub_iface_desc, act_len);
        return act_len;
    }
    case ENDPOINT: {
        int act_len;
        DHUBEM("Get endpoint descriptor\n");
        act_len = MIN(_hub_endpoint_desc.bLength, len);
        memcpy(buf, &_hub_endpoint_desc, act_len);
        return act_len;
    }
    case STRING                    :
    case DEVICE_QUALIFIER          :
    case OTHER_SPEED_CONFIGURATION :
    case INTERFACE_POWER           :
    default:
        printf("Descriptor 0x%x not supported\n", dtype);
        return -1;
    }
}


static int
hubem_feature(usb_hubem_t dev, struct usbreq* req)
{
    int f = req->wValue;
    int p = req->wIndex;
    void* t = dev->token;
    int ret;
    switch (req->bRequest) {
    case SET_FEATURE:
        DHUBEM("Set feature %d -> port %d\n", f, p);
        return dev->set_pf(t, p, f);
    case CLR_FEATURE:
        DHUBEM("Clear feature %d -> port %d\n", f, p);
        return dev->clr_pf(t, p, f);
    default:
        printf("Unsupported feature: %d\n", f);
        return -1;
    }
    return ret;
}

static int
hubem_get_status(usb_hubem_t dev, struct usbreq* req, void* buf, int len)
{
    int port = req->wIndex;
    if (port == 0) {
        /* Device status: self powered | remote wakeup */
        uint16_t stat = 0;
        int act_len;
        DHUBEM("Get Status: Device status\n");
        act_len = MIN(len, sizeof(stat));
        memcpy(buf, &stat, act_len);
        return act_len;
    } else if (port <= dev->hubem_nports) {
        /* Port status */
        struct port_status *psts = (struct port_status*)buf;
        int act_len = MIN(len, sizeof(*psts));
        assert(len >= sizeof(*psts));
        if (dev->get_pstat(dev->token, port, psts)) {
            DHUBEM("Get Status: Failed to read status for port %d\n", port);
            return -1;
        } else {
            DHUBEM("Get Status: Success s0x%x c0x%0x on port %d\n",
                   psts->wPortStatus, psts->wPortChange, port);
            return act_len;
        }
    } else {
        DHUBEM("Get Status: Invalid port (%d/%d)\n", port, dev->hubem_nports);
        return -1;
    }
}


int
hubem_process_xact(usb_hubem_t dev, struct xact* xact, int nxact,
                   usb_cb_t cb, void* t)
{
    struct usbreq* req;
    void* buf;
    int buf_len;
    int i;
    int err;

    assert(xact_get_vaddr(&xact[0]));
    for (err = 0, i = 0; !err && i < nxact; i++) {
        if (xact[i].type != PID_SETUP) {
            continue;
        }
        req = xact_get_vaddr(&xact[i]);
        assert(xact[i].len >= sizeof(*req));
        if (i + 1 < nxact && xact[i + 1].type != PID_SETUP) {
            buf = xact_get_vaddr(&xact[i + 1]);
            buf_len = xact[i + 1].len;
        } else {
            buf = NULL;
            buf_len = 0;
        }
        switch (req->bRequest) {
        case GET_STATUS:
            return hubem_get_status(dev, req, buf, buf_len);
        case GET_DESCRIPTOR:
            return hubem_get_descriptor(dev, req, buf, buf_len);
        case SET_CONFIGURATION:
            DHUBEM("Unhandled transaction: SET_CONFIGURATION\n");
            break;
        case SET_INTERFACE:
            DHUBEM("Unhandled transaction: SET_INTERFACE\n");
            break;
        case SET_ADDRESS:
            DHUBEM("Unhandled transaction: SET_ADDRESS\n");
            break;
        case CLR_FEATURE:
        case SET_FEATURE:
            err = hubem_feature(dev, req);
            break;
        default:
            printf("Request code %d not supported\n",
                   req->bRequest);
        }
    }
    if (cb) {
        if (err >= 0) {
            cb(t, XACTSTAT_SUCCESS, err);
        } else {
            cb(t, XACTSTAT_ERROR, err);
        }
    }
    return err;
}


int
usb_hubem_driver_init(void* token, int nports, int pwr_delay_ms,
                      int (*set_pf)(void *token, int port, enum port_feature feature),
                      int (*clr_pf)(void *token, int port, enum port_feature feature),
                      int (*get_pstat)(void* token, int port, struct port_status* ps),
                      usb_hubem_t* hub)
{

    usb_hubem_t h;
    h = (usb_hubem_t)usb_malloc(sizeof(*h));
    if (h == NULL) {
        assert(0);
        return -1;
    }

    h->token = token;
    h->hubem_nports = nports;
    h->pwr_delay_ms = pwr_delay_ms;
    h->set_pf = set_pf;
    h->clr_pf = clr_pf;
    h->get_pstat = get_pstat;
    *hub = h;
    return 0;
}


