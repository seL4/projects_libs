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

#include <sync/mutex.h>

#include "../services.h"
#include "storage.h"
#include "scsi.h"

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
    int            max_lun;   //Maximum logical unit number
    int            subclass;  //Industry standard
    int            protocol;  //Protocol code
    int            config;    //Selected configuration
    struct usb_endpoint ep_in;  //BULK in endpoint
    struct usb_endpoint ep_out; //BULK out endpoint
    struct usb_endpoint ep_int; //Interrupt endpoint(for CBI devices)
    sync_mutex_t   *mutex;
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

static void print_string_desc(struct string_desc *desc)
{
	assert(desc);

	for (int i = 0; i < desc->bLength - 2; i++) {
		printf("%c", desc->bString[i]);
	}
	printf("\n");
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
    struct endpoint_desc *edsc;
    struct usb_endpoint *ep;

    if (!desc) {
        return 0;
    }

    ubms = (struct usb_storage_device*)token;

    switch (desc->bDescriptorType) {
        case CONFIGURATION:
            cdesc = (struct config_desc*)desc;
	    ubms->config = cdesc->bConfigurationValue;
        case INTERFACE:
            idesc = (struct iface_desc*)desc;
            ubms->udev->class = idesc->bInterfaceClass;
            ubms->subclass = idesc->bInterfaceSubClass;
            ubms->protocol = idesc->bInterfaceProtocol;
            break;
	case STRING:
	    break;
        case ENDPOINT:
	    edsc = (struct endpoint_desc*)desc;
	    if (edsc->bmAttributes & EP_BULK) {
		    if (edsc->bEndpointAddress & (1 << 7)) {
			    ep = &ubms->ep_in;
		    } else {
			    ep = &ubms->ep_out;
		    }
	    } else if (edsc->bmAttributes & EP_INTERRUPT) {
		    ep = &ubms->ep_int;
	    } else {
		UBMS_DBG("Unknown EP: ep(%x), attr(%u), maxkpt(%u)\n",
			   edsc->bEndpointAddress,
			   edsc->bmAttributes, edsc->wMaxPacketSize);
		break;
	    }

	    ep->addr = edsc->bEndpointAddress & 0xF;
	    ep->max_pkt = edsc->wMaxPacketSize;
	    ep->dt = 0;
            break;
        default:
            break;
    }

    return 0;
}

//#define usb_storage_xfer_cb NULL
static int
usb_storage_xfer_cb(void* token, enum usb_xact_status stat, int rbytes)
{
	sync_mutex_t *mutex = (sync_mutex_t*)token;

	switch (stat) {
		case XACTSTAT_SUCCESS:
			printf("Success: %u\n", rbytes);
			break;
		case XACTSTAT_ERROR:
			printf("Error: %u\n", rbytes);
			break;
		case XACTSTAT_HOSTERROR:
			printf("Host err: %u\n", rbytes);
			break;
		default:
			assert(0);
			break;
	}

	sync_mutex_unlock(mutex);

	return 0;
}

void
usb_storage_set_configuration(usb_dev_t udev)
{
    int err;
    struct usb_storage_device *ubms;
    struct xact xact[2];
    struct usbreq *req;

    ubms = (struct usb_storage_device*)udev->dev_data;

    /* XXX: xact allocation relies on the xact.len */
    xact[0].len = sizeof(struct usbreq);
    xact[1].len = 0;

    /* Get memory for the request */
    err = usb_alloc_xact(udev->dman, &xact, 2);
    if (err) {
        UBMS_DBG("Not enough DMA memory!\n");
	assert(0);
    }

    /* Fill in the request */
    xact[0].type = PID_SETUP;
    xact[1].type = PID_IN;
    req = xact_get_vaddr(&xact[0]);
    *req = __set_configuration_req(ubms->config);

    /* Send the request to the host */
    err = usbdev_schedule_xact(udev, 0, udev->max_pkt, 0, 0, &xact, 2,
                               usb_storage_xfer_cb, (void*)ubms->mutex);
    sync_mutex_lock(ubms->mutex);
    usb_destroy_xact(udev->dman, &xact, 2);
    if (err < 0) {
        UBMS_DBG("USB mass storage set configuration failed.\n");
    }
}

void
usb_storage_get_string(usb_dev_t udev, int index, int lang, struct string_desc *desc)
{
    int err;
    struct usb_storage_device *ubms;
    struct xact xact[2];
    struct usbreq *req;
    struct string_desc *sdesc;

    ubms = (struct usb_storage_device*)udev->dev_data;

    /* XXX: xact allocation relies on the xact.len */
    xact[0].len = sizeof(struct usbreq);
    xact[1].len = sizeof(struct string_desc);

    /* Get memory for the request */
    err = usb_alloc_xact(udev->dman, xact, sizeof(xact) / sizeof(struct xact));
    if (err) {
        UBMS_DBG("Not enough DMA memory!\n");
        return;
    }

    /* Fill in the SETUP packet */
    xact[0].type = PID_SETUP;
    req = xact_get_vaddr(&xact[0]);
    *req = __get_descriptor_req(STRING, index, lang, sizeof(struct string_desc));

    /* Fill in the IN packet */
    xact[1].type = PID_IN;

    /* Send the request to the host */
    err = usbdev_schedule_xact(udev, 0, udev->max_pkt, 0, 0, xact, 2,
		               usb_storage_xfer_cb, (void*)ubms->mutex);
    sync_mutex_lock(ubms->mutex);

    if (err < 0) {
       UBMS_DBG("USB mass storage get LUN failed.\n");
       usb_destroy_xact(udev->dman, xact, 2);
       return;
    }

    sdesc = (struct string_desc*)xact[1].vaddr;
    memcpy(desc, sdesc, sdesc->bLength);
    usb_destroy_xact(udev->dman, xact, 2);
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
	assert(0);
    }

    /* Fill in the request */
    xact.type = PID_SETUP;
    req = xact_get_vaddr(&xact);
    *req = __get_reset_req(0);

    /* Send the request to the host */
    err = usbdev_schedule_xact(udev, 0, udev->max_pkt, 0, 0, &xact, 1,
		               usb_storage_xfer_cb, (void*)ubms->mutex);
    sync_mutex_lock(ubms->mutex);
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
    struct xact xact[3];
    struct usbreq *req;
    uint8_t max_lun;

    ubms = (struct usb_storage_device*)udev->dev_data;
    
    /* XXX: xact allocation relies on the xact.len */
    xact[0].len = sizeof(struct usbreq);
    xact[1].len = 1;
    xact[2].len = 0;

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
    xact[2].type = PID_OUT;

    /* Send the request to the host */
    err = usbdev_schedule_xact(udev, 0, udev->max_pkt, 0, 0, xact, 3,
		               usb_storage_xfer_cb, (void*)ubms->mutex);
    sync_mutex_lock(ubms->mutex);

    max_lun = *((uint8_t*)xact[1].vaddr);
    usb_destroy_xact(udev->dman, xact, 2);
    if (err < 0) {
       UBMS_DBG("USB mass storage get LUN failed.\n");
    }

    return max_lun;
}

int
usb_storage_bind(usb_dev_t udev, sync_mutex_t *mutex)
{
    int err;
    struct usb_storage_device *ubms;
    int class;
    struct string_desc desc;
    int language = 0;

    assert(udev);

    ubms = usb_malloc(sizeof(struct usb_storage_device));
    if (!ubms) {
        UBMS_DBG("Not enough memory!\n");
        return -1;
    }

    ubms->udev = udev;
    udev->dev_data = ubms;
    ubms->mutex = mutex;

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
    sync_mutex_lock(ubms->mutex);

    usb_storage_get_string(udev, 0, language, &desc);
    language = desc.bString[1] << 8 | desc.bString[0];
    usb_storage_get_string(udev, 2, language, &desc);
    print_string_desc(&desc);
    usb_storage_get_string(udev, 1, language, &desc);
    print_string_desc(&desc);
    usb_storage_get_string(udev, 3, language, &desc);
    print_string_desc(&desc);
    usb_storage_set_configuration(udev);
//    usb_storage_reset(udev);
    ubms->max_lun = usb_storage_get_max_lun(udev);

    scsi_init_disk(udev);

    return 0;
}

int
usb_storage_xfer(usb_dev_t udev, void *cb, size_t cb_len,
         struct xact *data, int ndata, int direction)
{
    int err, i, ret;
    struct cbw *cbw;
    struct csw *csw;
    struct xact xact[2];
    uint32_t tag;
    struct usb_storage_device *ubms;
    struct usb_endpoint *ep;

    ubms = (struct usb_storage_device*)udev->dev_data;

    /* Prepare command block */
    xact[0].type = PID_OUT;
    xact[0].len = sizeof(struct cbw);
    err = usb_alloc_xact(udev->dman, xact, 1);
    assert(!err);

    cbw = xact_get_vaddr(&xact[0]);
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
    ep = &ubms->ep_out;
    usb_storage_print_cbw(cbw);
    err = usbdev_schedule_xact(udev, ep->addr, ep->max_pkt, 0, ep->dt,
                               xact, 1, usb_storage_xfer_cb, (void*)ubms->mutex);
    sync_mutex_lock(ubms->mutex);
    ep->dt ^= 1;
    if (err < 0) {
        assert(0);
    }
    tag = cbw->tag;
    usb_destroy_xact(udev->dman, &xact, 1);

    /* Send/Receive data */
    if (data != NULL) {
        if (direction) {
            ep = &ubms->ep_in;
        }
        err = usbdev_schedule_xact(udev, ep->addr, ep->max_pkt, 0, ep->dt,
                                   data, ndata, usb_storage_xfer_cb,
                                   (void*)ubms->mutex);
        sync_mutex_lock(ubms->mutex);
        ep->dt ^= (ndata % 2);
        if (err < 0) {
            assert(0);
        }
    }

    /* Check CSW from IN endpoint */
    xact[0].len = sizeof(struct csw);
    xact[0].type = PID_IN;
    err = usb_alloc_xact(udev->dman, xact, 1);
    assert(!err);

    csw = xact_get_vaddr(&xact[0]);
    csw->signature = UBMS_CSW_SIGN;
    csw->tag = tag;

    ep = &ubms->ep_in;
    err = usbdev_schedule_xact(udev, ep->addr, ep->max_pkt, 0, ep->dt, xact, 1,
		               usb_storage_xfer_cb, (void*)ubms->mutex);
    sync_mutex_lock(ubms->mutex);
    ep->dt ^= 1;
    UBMS_DBG("CSW status(%u)\n", csw->status);
    if (err < 0) {
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

