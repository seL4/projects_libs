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
#include "ufi.h"

//#define MASS_STORAGE_DEBUG

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
    uint8_t lun;
    uint8_t cb_length;
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
    unsigned int   max_lun;   //Maximum logical unit number
    unsigned int   subclass;  //Industry standard
    unsigned int   protocol;  //Protocol code
    unsigned int   config;    //Selected configuration
    unsigned int   ep_in;     //BULK in endpoint
    unsigned int   ep_out;    //BULK out endpoint
    unsigned int   ep_int;    //Interrupt endpoint(for CBI devices)
};

static inline struct usbreq
__get_reset_req(int interface)
{
    struct usbreq r = {
        .bmRequestType = (USB_DIR_OUT | USB_TYPE_CLS | USB_RCPT_INTERFACE),
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
        .bmRequestType = (USB_DIR_IN | USB_TYPE_CLS | USB_RCPT_INTERFACE),
        .bRequest      = 0b11111110,
        .wValue        = 0,
        .wIndex        = interface,
        .wLength       = 1 
    };
    return r;
}

static void
usb_storage_print_cbw(struct cbw *cbw)
{
	assert(cbw);

	printf("==== CBW ====\n");
	printf("Signature: %x\n", cbw->signature);
	printf("Tag: %x\n", cbw->tag);
	printf("Length: %x\n", cbw->data_transfer_length);
	printf("Flag: %x\n", cbw->flags);
	printf("LUN: %x\n", cbw->lun);
	printf("CDB(%x): ", cbw->cb_length);
	for (int i = 0; i < cbw->cb_length; i++) {
		printf("%x, ", cbw->cb[i]);
	}
	printf("\n");
}

static int
usb_storage_config_cb(void* token, int cfg, int iface, struct anon_desc* desc)
{
    struct usb_storage_device *ubms;
    struct config_desc *cdesc;
    struct iface_desc *idesc;

    if (!desc) {
        return 0;
    }

    ubms = (struct usb_storage_device*)token;

    switch (desc->bDescriptorType) {
        case CONFIGURATION:
            cdesc = (struct config_desc*)desc;
	    ubms->config = cdesc->bConfigurationValue;
	    break;
        case INTERFACE:
            idesc = (struct iface_desc*)desc;
            ubms->udev->class = idesc->bInterfaceClass;
            ubms->subclass = idesc->bInterfaceSubClass;
            ubms->protocol = idesc->bInterfaceProtocol;
            break;
	case STRING:
	    break;
        default:
            break;
    }

    return 0;
}

void
usb_storage_set_configuration(usb_dev_t udev)
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
	assert(0);
    }

    /* Fill in the request */
    xact.type = PID_SETUP;
    req = xact_get_vaddr(&xact);
    *req = __set_configuration_req(ubms->config);

    /* Send the request to the host */
    err = usbdev_schedule_xact(udev, udev->ep_ctrl, &xact, 1, NULL, NULL);
    usb_destroy_xact(udev->dman, &xact, 1);
    if (err < 0) {
        UBMS_DBG("USB mass storage set configuration failed.\n");
    }
}

void
usb_storage_reset(usb_dev_t udev)
{
    int err;
    struct xact xact;
    struct usbreq *req;

    /* XXX: xact allocation relies on the xact.len */
    xact.len = sizeof(struct usbreq);

    /* Get memory for the request */
    err = usb_alloc_xact(udev->dman, &xact, 1);
    if (err) {
        UBMS_DBG("Not enough DMA memory!\n");
	assert(0);
    }

    /* Fill in the request */
    xact.type = PID_SETUP;
    req = xact_get_vaddr(&xact);
    *req = __get_reset_req(0);

    /* Send the request to the host */
    err = usbdev_schedule_xact(udev, udev->ep_ctrl, &xact, 1, NULL, NULL);
    usb_destroy_xact(udev->dman, &xact, 1);
    if (err < 0) {
        UBMS_DBG("USB mass storage reset failed.\n");
    }
}

int
usb_storage_get_max_lun(usb_dev_t udev)
{
    int err;
    struct xact xact[2];
    struct usbreq *req;
    uint8_t max_lun;

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
    err = usbdev_schedule_xact(udev, udev->ep_ctrl, xact, 2, NULL, NULL);

    max_lun = *((uint8_t*)xact[1].vaddr);
    usb_destroy_xact(udev->dman, xact, 2);
    if (err < 0) {
       UBMS_DBG("USB mass storage get LUN failed.\n");
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

    /* Find endpoints */
    for (int i = 0; udev->ep[i] != NULL; i++) {
	    if (udev->ep[i]->type == EP_BULK) {
		    if (udev->ep[i]->dir == EP_DIR_OUT) {
			    ubms->ep_out = i;
		    } else {
			    ubms->ep_in = i;
		    }
	    } else if (udev->ep[i]->type == EP_INTERRUPT) {
		    ubms->ep_int = i;
	    } else {
		    continue;
	    }
    }

    class = usbdev_get_class(udev);
    if (class != USB_CLASS_STORAGE) {
        UBMS_DBG("Not a USB mass storage(%d)\n", class);
	usb_free(ubms);
        return -1;
    }

    UBMS_DBG("USB storage found, subclass(%x, %x)\n", ubms->subclass, ubms->protocol);

    usb_storage_set_configuration(udev);
//    usb_storage_reset(udev);
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
    struct usb_storage_device *ubms;
    struct endpoint *ep;

    ubms = (struct usb_storage_device*)udev->dev_data;

    /* Prepare command block */
    xact.type = PID_OUT;
    xact.len = sizeof(struct cbw);
    err = usb_alloc_xact(udev->dman, &xact, 1);
    assert(!err);

    cbw = xact_get_vaddr(&xact);
    cbw->signature = UBMS_CBW_SIGN;
    cbw->tag = 0;
    cbw->data_transfer_length = 0;
    for (i = 0; i < ndata; i++) {
        cbw->data_transfer_length += data[i].len;
    }
    cbw->flags = (direction & 0x1) << 7;
    cbw->lun = 0; //TODO: multiple LUN
    cbw->cb_length = cb_len;
    memcpy(cbw->cb, cb, cb_len);

    /* Send CBW */
    ep = udev->ep[ubms->ep_out];
#ifdef MASS_STORAGE_DEBUG
    usb_storage_print_cbw(cbw);
#endif
    err = usbdev_schedule_xact(udev, ep, &xact, 1, NULL, NULL);
    if (err < 0) {
        assert(0);
    }
    tag = cbw->tag;
    usb_destroy_xact(udev->dman, &xact, 1);

    msdelay(200);
    /* Send/Receive data */
    if (data != NULL) {
        if (direction) {
            ep = udev->ep[ubms->ep_in];
        }
        err = usbdev_schedule_xact(udev, ep, data, ndata, NULL, NULL);
        if (err < 0) {
            assert(0);
        }
    }
    msdelay(200);

    /* Check CSW from IN endpoint */
    xact.len = sizeof(struct csw);
    xact.type = PID_IN;
    err = usb_alloc_xact(udev->dman, &xact, 1);
    assert(!err);

    csw = xact_get_vaddr(&xact);
    csw->signature = UBMS_CSW_SIGN;
    csw->tag = tag;

    ep = udev->ep[ubms->ep_in];
    err = usbdev_schedule_xact(udev, ep, &xact, 1, NULL, NULL);
    UBMS_DBG("CSW status(%u)\n", csw->status);
    if (err < 0) {
        assert(0);
    }

    switch (csw->status) {
        case CSW_STS_PASS:
            ret = 0;
            break;
        case CSW_STS_FAIL:
//            assert(0);
            ret = 0;
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

/* Exported Interface */
int usb_storage_init_disk(usb_dev_t usb_dev)
{
	return ufi_init_disk(usb_dev);
}
uint32_t usb_storage_get_capacity(usb_dev_t usb_dev)
{
	return ufi_read_capacity(usb_dev);
}

int usb_storage_write(usb_dev_t usb_dev, void *buf, int size)
{
	return 0;
}

int usb_storage_read(usb_dev_t usb_dev, void *buf, int size)
{
	return 0;
}

