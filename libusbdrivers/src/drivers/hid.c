/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

/**
 * @brief USB HID subclass driver
 * @see USB HID subclass specification
 */
#include <stdio.h>
#include <string.h>

#include "../services.h"
#include "hid.h"
#include "usbkbd.h"

static inline struct usbreq
__set_protocol_req(enum hid_protocol p, int iface) {
    struct usbreq r = {
        .bmRequestType = (USB_DIR_OUT | USB_TYPE_CLS | USB_RCPT_INTERFACE),
        .bRequest      = SET_PROTOCOL,
        .wValue        = p,
        .wIndex        = iface,
        .wLength       = 0
    };
    return r;
}

static inline struct usbreq
__get_protocol_req(int iface) {
    struct usbreq r = {
        .bmRequestType = (USB_DIR_IN | USB_TYPE_CLS | USB_RCPT_INTERFACE),
        .bRequest      = GET_PROTOCOL,
        .wValue        = 0,
        .wIndex        = iface,
        .wLength       = 1
    };
    return r;
}

static inline struct usbreq
__set_idle_req(int idle_ms, int id, int iface) {
    struct usbreq r = {
        .bmRequestType = (USB_DIR_OUT | USB_TYPE_CLS | USB_RCPT_INTERFACE),
        .bRequest      = SET_IDLE,
        .wValue        = idle_ms << 8 | id,
        .wIndex        = iface,
        .wLength       = 0
    };
    return r;
}


static inline struct usbreq
__get_report_req(enum hid_report_type type, int id, int iface, int len) {
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
__set_report_req(enum hid_report_type type, int id, int iface, int len) {
    struct usbreq r = {
        .bmRequestType = (USB_DIR_OUT | USB_TYPE_CLS | USB_RCPT_INTERFACE),
        .bRequest      = SET_REPORT,
        .wValue        = type << 8 | id,
        .wIndex        = iface,
        .wLength       = len
    };
    return r;
}

static int
usb_hid_config_cb(void *token, int cfg, int iface, struct anon_desc *desc)
{
	struct usb_hid_device *hid;
	struct config_desc *cdesc;
	struct iface_desc *idesc;
	struct hid_desc *hdesc;

	if (!desc) {
		return 0;
	}

	hid = (struct usb_hid_device *)token;

	switch (desc->bDescriptorType) {
	case CONFIGURATION:
		cdesc = (struct config_desc*)desc;
		hid->config = cdesc->bConfigurationValue;
		break;
	case INTERFACE:
		idesc = (struct iface_desc *)desc;
		if (idesc->bInterfaceSubClass == 1) {
			hid->subclass = idesc->bInterfaceSubClass;
			hid->protocol = idesc->bInterfaceProtocol;
			hid->iface = idesc->bInterfaceNumber;
		}
		break;
	case HID:
		hdesc = (struct hid_desc *)desc;
		if (!hid->subclass) {
			if (hdesc->bReportDescriptorType== HID_REPORT) {
				hid->report_size = hdesc->wReportDescriptorLength;
			} else {
				ZF_LOGD("Descriptor %u not supported!\n",
					hdesc->bReportDescriptorType);
			}
		}
		break;
	default:
		break;
	}

	return 0;
}

struct usb_hid_device *usb_hid_alloc(struct usb_dev *udev)
{
	int err;
	struct usb_hid_device *hid;
	struct xact xact;
	struct usbreq *req;
	int class;

	if (!udev) {
		ZF_LOGF("Invalid device\n");
	}

	hid = usb_malloc(sizeof(struct usb_hid_device));
	if (!hid) {
		ZF_LOGD("Not enough memory!\n");
		return NULL;
	}

	hid->udev = udev;
	hid->subclass = 0xFF;

	/* Parse the descriptors */
	err = usbdev_parse_config(udev, usb_hid_config_cb, hid);
	if (err) {
		ZF_LOGF("Invalid descriptors\n");
	}

	class = usbdev_get_class(udev);
	if (class != USB_CLASS_HID) {
		ZF_LOGD("Not a HID device(%d)\n", class);
		usb_free(hid);
		return NULL;
	}

	/* Activate configuration */
	xact.len = sizeof(struct usbreq);
	err = usb_alloc_xact(udev->dman, &xact, 1);
	if (err) {
		ZF_LOGF("Out of DMA memory\n");
	}

	/* Fill in the request */
	xact.type = PID_SETUP;
	req = xact_get_vaddr(&xact);
	*req = __set_configuration_req(hid->config);

	/* Send the request to the host */
	err = usbdev_schedule_xact(udev, udev->ep_ctrl, &xact, 1, NULL, NULL);
	if (err) {
		ZF_LOGF("Transaction error\n");
	}

	usb_destroy_xact(udev->dman, &xact, 1);

	return hid;
}

void usb_hid_free(struct usb_hid_device *hid)
{
	usb_free(hid);
}

int usb_hid_set_idle(struct usb_hid_device *hid, int idle_ms)
{
	int err;
	struct usbreq *req;
	struct xact xact;

	xact.type = PID_SETUP;
	xact.len = sizeof(struct usbreq);

	err = usb_alloc_xact(hid->udev->dman, &xact, 1);
	if (err) {
		ZF_LOGF("Out of DMA memory\n");
	}

	req = xact_get_vaddr(&xact);
	*req = __set_idle_req(idle_ms, 0, hid->iface);

	err = usbdev_schedule_xact(hid->udev, hid->udev->ep_ctrl, &xact, 1, NULL, NULL);
	if (err) {
		ZF_LOGF("Transaction error\n");
	}

	usb_destroy_xact(hid->udev->dman, &xact, 1);

	return err;
}

int usb_hid_get_idle(struct usb_hid_device *hid)
{
	return 0;
}

int usb_hid_get_report(struct usb_hid_device *hid, enum hid_report_type type, void *buf)
{
	return 0;
}

int usb_hid_set_report(struct usb_hid_device *hid, enum hid_report_type type,
		void *buf, int size)
{
	int err;
	struct usbreq *req;
	struct xact xact[2];

	xact[0].type = PID_SETUP;
	xact[0].len = sizeof(struct usbreq);
	xact[1].type = PID_OUT;
	xact[1].len = size;

	err = usb_alloc_xact(hid->udev->dman, xact, 2);
	if (err) {
		ZF_LOGF("Out of DMA memory\n");
	}

	req = xact_get_vaddr(&xact[0]);
	*req = __set_report_req(REPORT_OUTPUT, 0, hid->iface, size);

	memcpy(xact_get_vaddr(&xact[1]), buf, size);

	err = usbdev_schedule_xact(hid->udev, hid->udev->ep_ctrl, xact, 2, NULL, NULL);
	if (err) {
		ZF_LOGF("Transaction error\n");
	}

	usb_destroy_xact(hid->udev->dman, xact, 2);

	return err;
}

int usb_hid_get_protocol(struct usb_hid_device *hid)
{
	return 0;
}

int usb_hid_set_protocol(struct usb_hid_device *hid)
{
	return 0;
}

