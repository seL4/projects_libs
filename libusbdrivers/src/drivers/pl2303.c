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

/* PL2303 USB to Serial Converter */
struct pl2303_device {
	usb_dev_t udev;	         //The handle to the underlying USB device
	uint8_t config;          //Active configuration
	struct endpoint *ep_int; //Interrupt endpoint
	struct endpoint *ep_in;	 //BULK in endpoint
	struct endpoint *ep_out; //BULK out endpoint
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

	return 0;
}

