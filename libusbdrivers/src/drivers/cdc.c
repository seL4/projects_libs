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

#include "../services.h"
#include "cdc.h"

#define USB_CDC_DEBUG

#ifdef USB_CDC_DEBUG
#define CDC_DBG(...)            \
        do {                     \
            printf("CDC: ");    \
            printf(__VA_ARGS__); \
        }while(0)
#else
#define CDC_DBG(...) do{}while(0)
#endif

static const char *subclass_codes[] = {
	"Reserved",
	"Direct Line Control Model",
	"Abstract Control Model",
	"Telephone Control Model",
	"Multi-Channel Control Model",
	"CAPI Control Model",
	"Ethernet Networking Control Model",
	"ATM Networking Control Model",
	"Wireless Handset Control Model",
	"Device Management",
	"Mobile Direct Line Model",
	"OBEX",
	"Ethernet Emulation Mode",
	"Mobile Broadband Interface Model"
};

static const char *func_subtype_codes[] = {
	"Header Functional",
	"Call Management Functional",
	"Abstract Control Management Functional",
	"Direct Line Management Functional",
	"Telephone Ringer Functional",
	"Telephone Call and Line State Reporting Capabilities Functional",
	"Union Functional",
	"Country Selection Functional",
	"Telephone Operational Modes Functional",
	"USB Terminal Functional",
	"Network Channel Terminal",
	"Protocol Unit Functional",
	"Extension Unit Functional",
	"Multi-Channel Management Functional",
	"CAPI Control Management Functional",
	"Ethernet Networking Functional",
	"ATM Networking Functional",
	"Wireless Handset Control Model Functional",
	"Mobile Direct Line Model Functional",
	"MDLM Detail Functional",
	"Device Management Model Functional",
	"OBEX Functional",
	"Command Set Functional",
	"Command Set Detail Functional",
	"Telephone Control Model Functional",
	"OBEX Service Identifier Functional",
	"NCM Functional"
	"MBIM Functional",
	"MBIM Extended Functional",
	"RESERVED (future use)",
	"RESERVED (vendor specific)"
};

/* CDC device interface class */
enum usb_cdc_inf_class {
	INF_COMM = 0x2,  //Communication Interface Class
	INF_DATA = 0xA   //Data Interface Class
};

/* USB Communication Device */
struct usb_cdc_device {
	usb_dev_t udev;		//The handle to the underlying USB device
	unsigned int subclass;	//Subclass code
	unsigned int protocol;	//Protocol code
	unsigned int ep_int;	//Interrupt endpoint
	unsigned int ep_in;	//BULK in endpoint
	unsigned int ep_out;	//BULK out endpoint
};

static int
usb_cdc_config_cb(void *token, int cfg, int iface, struct anon_desc *desc)
{
	struct usb_cdc_device *cdc;
	struct config_desc *cdesc;
	struct iface_desc *idesc;
	struct func_desc *fdesc;
	struct endpoint_desc *edsc;
	struct endpoint *ep;

	if (!desc) {
		return 0;
	}

	cdc = (struct usb_cdc_device *)token;

	switch (desc->bDescriptorType) {
	case INTERFACE:
		idesc = (struct iface_desc *)desc;
		cdc->udev->class = idesc->bInterfaceClass;
		cdc->subclass = idesc->bInterfaceSubClass;
		cdc->protocol = idesc->bInterfaceProtocol;
		if (cdc->udev->class == INF_COMM && cdc->subclass < 0xd) {
			CDC_DBG("Communication Interface\n");
			if (cdc->subclass < 0xd) {
				CDC_DBG("  |-- %s\n", subclass_codes[cdc->subclass]);
			}
		} else if (cdc->udev->class == INF_DATA) {
			CDC_DBG("Data Interface\n");
		}
		break;
	case CS_INTERFACE:
		fdesc = (struct func_desc *)desc;
		if (fdesc->bDescriptorSubtype < 0x1d) {
			CDC_DBG("  %s\n",
					func_subtype_codes[fdesc->bDescriptorSubtype]);
		} else {
			CDC_DBG("  Function type reserved(%x)\n",
					fdesc->bDescriptorSubtype);
		}
		break;
	default:
		break;
	}

	return 0;
}

int usb_cdc_bind(usb_dev_t udev)
{
	int err;
	struct usb_cdc_device *cdc;
	int class;

	assert(udev);

	cdc = usb_malloc(sizeof(struct usb_cdc_device));
	if (!cdc) {
		CDC_DBG("Not enough memory!\n");
		return -1;
	}

	cdc->udev = udev;
	udev->dev_data = cdc;

	/* Parse the descriptors */
	err = usbdev_parse_config(udev, usb_cdc_config_cb, cdc);
	assert(!err);

	/* Find endpoints */
	for (int i = 0; udev->ep[i] != NULL; i++) {
		if (udev->ep[i]->type == EP_BULK) {
			if (udev->ep[i]->dir == EP_DIR_OUT) {
				cdc->ep_out = i;
			} else {
				cdc->ep_in = i;
			}
		} else if (udev->ep[i]->type == EP_INTERRUPT) {
			cdc->ep_int = i;
		} else {
			continue;
		}
	}

	class = usbdev_get_class(udev);
	if (class != USB_CLASS_STORAGE) {
		CDC_DBG("Not a CDC device(%d)\n", class);
		return -1;
	}

	CDC_DBG("USB CDC found, subclass(%x, %x)\n", cdc->subclass,
		cdc->protocol);

	return 0;
}

