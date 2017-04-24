/*
 * Copyright 2016, NICTA
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

#include <usb/drivers/pl2303.h>
#include "../services.h"

#define USB_PL2303_DEBUG

#ifdef USB_PL2303_DEBUG
#define PL2303_DBG(...)            \
        do {                     \
            printf("pl2303: ");    \
            printf(__VA_ARGS__); \
        }while(0)
#else
#define PL2303_DBG(...) do{}while(0)
#endif

#define PL2303_VENDOR_REQ  0x01
#define PL2303_READ_TYPE   (USB_DIR_IN | USB_TYPE_VEN | USB_RCPT_DEVICE)
#define PL2303_WRITE_TYPE  (USB_DIR_OUT | USB_TYPE_VEN | USB_RCPT_DEVICE)

#define PL2303_SET_LINE_REQ 0x20
#define PL2303_SET_CTRL_REQ 0x22
#define PL2303_SET_REQ_TYPE 0x21

#define PL2303_GET_LINE_REQ 0x21
#define PL2303_GET_REQ_TYEP 0xA1

#define PL2303_CTRL_DTR     0x01
#define PL2303_CTRL_RTS     0x01

/* PL2303 USB to Serial Converter */
struct pl2303_device {
	usb_dev_t udev;	         //The handle to the underlying USB device
	uint8_t config;          //Active configuration
	struct endpoint *ep_int; //Interrupt endpoint
	struct endpoint *ep_in;	 //BULK in endpoint
	struct endpoint *ep_out; //BULK out endpoint
	struct xact int_xact;    //Interrupt xact
};

static int
pl2303_config_cb(void *token, int cfg, int iface, struct anon_desc *desc)
{
	struct pl2303_device *dev;
	struct config_desc *cdesc;

	if (!desc) {
		return 0;
	}

	dev = (struct pl2303_device*)token;

	switch (desc->bDescriptorType) {
	case CONFIGURATION:
		cdesc = (struct config_desc*)desc;
		dev->config = cdesc->bConfigurationValue;
		break;
	default:
		break;
	}

	return 0;
}

static int
pl2303_interrupt_cb(void* token, enum usb_xact_status stat, int rbytes)
{
	int err;
	struct pl2303_device *dev;
	usb_dev_t udev;

	udev = (usb_dev_t)token;
	dev = (struct pl2303_device*)udev->dev_data;

	/* Queue another request */
	err = usbdev_schedule_xact(udev, dev->ep_int, &dev->int_xact, 1,
			pl2303_interrupt_cb, udev);
	assert(!err);

	return err;
}

static void
pl2303_startup_magic(usb_dev_t udev)
{
	int err;
	struct xact xact[2];
	struct usbreq *req;

	assert(udev);

	xact[0].len = sizeof(struct usbreq);
	xact[1].len = 1;
	err = usb_alloc_xact(udev->dman, xact, 2);
	assert(!err);

	/* Fill in the request */
	xact[0].type = PID_SETUP;
	xact[1].type = PID_IN;
	req = xact_get_vaddr(&xact[0]);

	/* Perform magic */
	req->bRequest = PL2303_VENDOR_REQ;

#define magic_request(typ, idx, val) \
	req->bmRequestType = typ; \
	req->wIndex = idx; \
	req->wValue = val; \
	req->wLength = (typ == PL2303_READ_TYPE) ? 1 : 0; \
	err = usbdev_schedule_xact(udev, udev->ep_ctrl, xact, \
			req->wLength + 1, NULL, NULL); \
	assert(!err);

	magic_request(PL2303_READ_TYPE, 0, 0x8484);
	magic_request(PL2303_WRITE_TYPE, 0, 0x0404);
	magic_request(PL2303_READ_TYPE, 0, 0x8484);
	magic_request(PL2303_READ_TYPE, 0, 0x8383);
	magic_request(PL2303_READ_TYPE, 0, 0x8484);
	magic_request(PL2303_WRITE_TYPE, 1, 0x0404);
	magic_request(PL2303_READ_TYPE, 0, 0x8484);
	magic_request(PL2303_READ_TYPE, 0, 0x8383);
	magic_request(PL2303_WRITE_TYPE, 1, 0);
	magic_request(PL2303_WRITE_TYPE, 0, 1);
	magic_request(PL2303_WRITE_TYPE, 0x44, 2);

	usb_destroy_xact(udev->dman, xact, 2);
}

int usb_pl2303_bind(usb_dev_t udev)
{
	int err;
	struct pl2303_device *dev;
	struct xact xact;
	struct usbreq *req;

	assert(udev);

	dev = usb_malloc(sizeof(struct pl2303_device));
	if (!dev) {
		PL2303_DBG("Not enough memory!\n");
		return -1;
	}

	dev->udev = udev;
	udev->dev_data = (struct udev_priv*)dev;

	/* Parse the descriptors */
	err = usbdev_parse_config(udev, pl2303_config_cb, dev);
	assert(!err);

	/* Find endpoints */
	for (int i = 0; udev->ep[i] != NULL; i++) {
		if (udev->ep[i]->type == EP_BULK) {
			if (udev->ep[i]->dir == EP_DIR_OUT) {
				dev->ep_out = udev->ep[i];
			} else {
				dev->ep_in = udev->ep[i];
			}
		} else if (udev->ep[i]->type == EP_INTERRUPT) {
			dev->ep_int = udev->ep[i];
		} else {
			continue;
		}
	}

	if (udev->vend_id != 0x067b || udev->prod_id != 0x2303) {
		PL2303_DBG("Not a PL2303 device(%u:%u)\n",
				udev->vend_id, udev->prod_id);
		return -1;
	}

	PL2303_DBG("Found PL2303 USB to serial converter!\n");

	/* Activate configuration */
	xact.len = sizeof(struct usbreq);
	err = usb_alloc_xact(udev->dman, &xact, 1);
	assert(!err);

	/* Fill in the request */
	xact.type = PID_SETUP;
	req = xact_get_vaddr(&xact);
	*req = __set_configuration_req(dev->config);

	/* Send the request to the host */
	err = usbdev_schedule_xact(udev, udev->ep_ctrl, &xact, 1, NULL, NULL);
	assert(!err);
	usb_destroy_xact(udev->dman, &xact, 1);

	pl2303_startup_magic(udev);

	/* Allocate interrupt xact */
	dev->int_xact.type = PID_IN;
	dev->int_xact.len = dev->ep_int->max_pkt;
	err = usb_alloc_xact(udev->dman, &dev->int_xact, 1);
	assert(!err);

	/* Schedule a interrupt request */
	err = usbdev_schedule_xact(udev, dev->ep_int, &dev->int_xact, 1,
			pl2303_interrupt_cb, udev);
	assert(!err);

	return 0;
}

int usb_pl2303_configure(usb_dev_t udev, uint32_t bps, uint8_t char_size,
		enum serial_parity parity, uint8_t stop)
{
	int err;
	struct xact xact[2];
	struct usbreq *req;
	char *buf;

	xact[0].len = sizeof(struct usbreq);
	xact[1].len = 7;
	err = usb_alloc_xact(udev->dman, xact, 2);
	assert(!err);

	xact[0].type = PID_SETUP;
	xact[1].type = PID_OUT;
	req = xact_get_vaddr(&xact[0]);
	buf = xact_get_vaddr(&xact[1]);

	/* Data bits */
	buf[6] = char_size;

	/*
	 * Set parity
	 * 0 -- none, 1 -- odd, 2-- even, 3 -- mark, 4 -- space
	 */
	switch (parity) {
		case PARITY_NONE:
			buf[5] = 0;
			break;
		case PARITY_ODD:
			buf[5] = 1;
			break;
		case PARITY_EVEN:
			buf[5] = 2;
			break;
		default:
			PL2303_DBG("Unsupported parity!\n");
			break;
	}

	/* 0 -- 1 stop bits, 1 -- 1.5 stop bits, 2 -- 2 stop bits */
	buf[4] = stop;

	/* Set baudrate */
	buf[3] = bps & 0xFF;
	buf[2] = (bps >> 16) & 0xFF;
	buf[1] = (bps >> 8) & 0xFF;
	buf[0] = bps & 0xFF000000;

	/* Send configuration */
	req->bmRequestType = PL2303_SET_REQ_TYPE;
	req->bRequest = PL2303_SET_LINE_REQ;
	req->wIndex = 0;
	req->wLength = 7;
	req->wValue = 0;

	err = usbdev_schedule_xact(udev, udev->ep_ctrl, xact, 2, NULL, NULL);
	assert(!err);

	/* Activate device */
	req->bmRequestType = PL2303_SET_REQ_TYPE;
	req->bRequest = PL2303_SET_CTRL_REQ;
	req->wIndex = 0;
	req->wLength = 0;
	req->wValue = PL2303_CTRL_DTR | PL2303_CTRL_RTS;

	err = usbdev_schedule_xact(udev, udev->ep_ctrl, xact, 1, NULL, NULL);
	assert(!err);

	usb_destroy_xact(udev->dman, xact, 2);

	return 0;
}

/* TODO: Remove the 20K limitation */
int usb_pl2303_write(usb_dev_t udev, void *buf, int len)
{
	int err;
	struct pl2303_device *dev;
	struct xact xact;

	dev = (struct pl2303_device*)udev->dev_data;

	xact.type = PID_OUT;
	xact.len = len;
	err = usb_alloc_xact(udev->dman, &xact, 1);
	assert(!err);

	memcpy(xact_get_vaddr(&xact), buf, len);

	err = usbdev_schedule_xact(udev, dev->ep_out, &xact, 1, NULL, NULL);
	assert(!err);

	usb_destroy_xact(udev->dman, &xact, 1);

	return len - err;
}

/* TODO: Remove the 20K limitation and possible babble */
int usb_pl2303_read(usb_dev_t udev, void *buf, int len)
{
	int err;
	struct pl2303_device *dev;
	struct xact xact;

	dev = (struct pl2303_device*)udev->dev_data;

	xact.type = PID_IN;
	xact.len = len;
	err = usb_alloc_xact(udev->dman, &xact, 1);
	assert(!err);

	err = usbdev_schedule_xact(udev, dev->ep_in, &xact, 1, NULL, NULL);
	assert(err >= 0);

	memcpy(buf, xact_get_vaddr(&xact), len);

	usb_destroy_xact(udev->dman, &xact, 1);

	return len - err;
}

