/*
 * Copyright 2015, NICTA
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(NICTA_BSD)
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "../services.h"
#include "storage.h"

#define MASS_STORAGE_DEBUG

#ifdef MASS_STORAGE_DEBUG
#define UBMS_DBG(...)            \
        do {                     \
            printf("UBMS: ");    \
            printf(__VA_ARGS__); \
        }while(0)
#else
#define UBMS_DBG(...) do{}while(0)
#endif

#define UBMS_CBW_SIGN 0x43425355 //Command block wrapper signature
#define UBMS_CSW_SIGN 0x53425355 //Command status wrapper signature

#define CSW_STS_PASS 0x0
#define CSW_STS_FAIL 0x1
#define CSW_STS_ERR  0x2

/* Command Block Wrapper */
struct cbw {
    uint32_t signature;
    uint32_t tag;
    uint32_t data_transfer_length;
    uint8_t flags;
    uint8_t lun:4;
    uint8_t cb_length:5;
    uint8_t cb[16];
} __attribute__((packed));

/* Command Status Wrapper */
struct csw {
    uint32_t signature;
    uint32_t tag;
    uint32_t residue;
    uint8_t status;
} __attribute__((packed));

/* USB mass storage device */
struct usb_storage_device {
    usb_dev_t      udev;      //The handle to the underlying USB device
    int            max_lun;   //Maximum logical unit number
    int            subclass;  //Industry standard
    int            protocol;  //Protocol code
};

static inline struct usbreq
__get_reset_req(int interface)
{
    struct usbreq r = {
        .bmRequestType = 0b00100001,
        .bRequest      = 0b11111111,
        .wValue        = 0,
        .wIndex        = interface,
        .wLength       = 0 
    };
    return r;
}

static inline struct usbreq
__get_max_lun_req(int interface)
{
    struct usbreq r = {
        .bmRequestType = 0b10100001,
        .bRequest      = 0b11111110,
        .wValue        = 0,
        .wIndex        = interface,
        .wLength       = 1 
    };
    return r;
}

static int
usb_storage_config_cb(void* token, int cfg, int iface, struct anon_desc* desc)
{
    struct usb_storage_device *ubms;
    struct iface_desc *idesc;

    if (!desc) {
        return 0;
    }

    ubms = (struct usb_storage_device*)token;

    switch (desc->bDescriptorType) {
        case INTERFACE:
            idesc = (struct iface_desc*)desc;
            ubms->udev->class = idesc->bInterfaceClass;
            ubms->subclass = idesc->bInterfaceSubClass;
            ubms->protocol = idesc->bInterfaceProtocol;
            break;
        case ENDPOINT:
            break;
        default:
            break;
    }

    return 0;
}

void
usb_storage_reset(usb_dev_t udev)
{
    int err;
    struct usb_storage_device *ubms;
    struct xact xact;
    struct usbreq *req;

    ubms = (struct usb_storage_device*)udev->dev_data;

    /* XXX: xact allocation relies on the xact.len */
    xact.len = sizeof(struct usbreq);

    /* Get memory for the request */
    err = usb_alloc_xact(udev->dman, &xact, 1);
    if (err) {
    UBMS_DBG("Not enough DMA memory!\n");
        return -1;
    }

    /* Fill in the request */
    xact.type = PID_SETUP;
    req = xact_get_vaddr(&xact);
    *req = __get_reset_req(0);

    /* Send the request to the host */
    err = usbdev_schedule_xact(udev, 0, udev->max_pkt, 0, &xact, 1, NULL, NULL);
    usb_destroy_xact(udev->dman, &xact, 1);
    if (err < 0) {
        UBMS_DBG("USB mass storage reset failed.\n");
    }
}

int
usb_storage_get_max_lun(usb_dev_t udev)
{
    int err;
    struct usb_storage_device *ubms;
    struct xact xact[2];
    struct usbreq *req;
    uint8_t max_lun;

    ubms = (struct usb_storage_device*)udev->dev_data;
    
    /* XXX: xact allocation relies on the xact.len */
    xact[0].len = sizeof(struct usbreq);
    xact[1].len = 1;

    /* Get memory for the request */
    err = usb_alloc_xact(udev->dman, xact, sizeof(xact) / sizeof(struct xact));
    if (err) {
        UBMS_DBG("Not enough DMA memory!\n");
        return -1;
    }

    /* Fill in the SETUP packet */
    xact[0].type = PID_SETUP;
    req = xact_get_vaddr(&xact[0]);
    *req = __get_max_lun_req(0);

    /* Fill in the IN packet */
    xact[1].type = PID_IN;

    /* Send the request to the host */
    err = usbdev_schedule_xact(udev, 0, udev->max_pkt, 0, &xact, 2, NULL, NULL);

    max_lun = *((uint8_t*)xact[1].vaddr);
    usb_destroy_xact(udev->dman, xact, 2);
    if (err < 0) {
       UBMS_DBG("USB mass storage reset failed.\n");
    }

    return max_lun;
}

int
usb_storage_bind(usb_dev_t udev)
{
    int err;
    struct usb_storage_device *ubms;
    int class;

    assert(udev);

    ubms = usb_malloc(sizeof(struct usb_storage_device));
    if (!ubms) {
        UBMS_DBG("Not enough memory!\n");
        return -1;
    }

    ubms->udev = udev;
    udev->dev_data = ubms;

    /*
     * Check if this is a storage device.
     * The class info is stored in the interface descriptor.
     */
    err = usbdev_parse_config(udev, usb_storage_config_cb, ubms);
    assert(!err);

    class = usbdev_get_class(udev);
    if (class != USB_CLASS_STORAGE) {
        UBMS_DBG("Not a USB mass storage(%d)\n", class);
        return -1;
    }

    UBMS_DBG("USB storage found, subclass(%x, %x)\n", ubms->subclass, ubms->protocol);

    usb_storage_reset(udev);
    ubms->max_lun = usb_storage_get_max_lun(udev);

    return 0;
}

int
usb_storage_xfer(usb_dev_t udev, void *cb, size_t cb_len,
         struct xact *data, int ndata, int direction)
{
    int err, i, ret;
    struct cbw *cbw;
    struct csw *csw;
    struct xact xact;
    uint32_t tag;

    /* Prepare command block */
    xact.len = sizeof(struct cbw);
    err = usb_alloc_xact(udev->dman, &xact, 1);
    assert(!err);

    cbw = xact_get_vaddr(&xact);
    cbw->signature = UBMS_CBW_SIGN;
    cbw->data_transfer_length = 0;
    for (i = 0; i < ndata; i++) {
        cbw->data_transfer_length += data[i].len;
    }
    cbw->flags = (direction & 0x1) << 7;
    cbw->lun = 0; //TODO: multiple LUN
    cbw->cb_length = cb_len;
    memcpy(cbw->cb, cb, cb_len);

    /* Send CBW */
    err = usbdev_schedule_xact(udev, 1, udev->max_pkt, 0, &xact, 1, NULL, NULL);
    if (!err) {
        assert(0);
    }
    tag = cbw->tag;
    usb_destroy_xact(udev->dman, &xact, 1);

    /* Send/Receive data */
    err = usbdev_schedule_xact(udev, direction & 0x1, udev->max_pkt, 0,
                               data, ndata, NULL, NULL);
    if (!err) {
        assert(0);
    }

    /* Check CSW from IN endpoint */
    xact.len = sizeof(struct csw);
    err = usb_alloc_xact(udev->dman, &xact, 1);
    assert(!err);

    csw = xact_get_vaddr(&xact);
    csw->signature = UBMS_CSW_SIGN;
    csw->tag = tag;

    err = usbdev_schedule_xact(udev, 1, udev->max_pkt, 0, &xact, 1, NULL, NULL);
    UBMS_DBG("CSW status(%u)\n", csw->status);
    if (!err) {
        assert(0);
    }

    switch (csw->status) {
        case CSW_STS_PASS:
            ret = 0;
            break;
        case CSW_STS_FAIL:
            assert(0);
            ret = -1;
            break;
        case CSW_STS_ERR:
            assert(0);
            ret = -2;
            break;
        default:
            UBMS_DBG("Unknown CSW status(%u)\n", csw->status);
            ret = -3;
            break;
    }

    usb_destroy_xact(udev->dman, &xact, 1);

    return ret;

}

