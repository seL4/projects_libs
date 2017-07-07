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

#include <utils/util.h>

#include "../services.h"

#define HUBINT_RATE_MS 100

#define HUB_ENABLE_IRQS

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
__clear_port_feature_req(uint16_t port, uint16_t feature)
{
	struct usbreq r = {
		.bmRequestType = (USB_DIR_OUT | USB_TYPE_CLS | USB_RCPT_OTHER),
		.bRequest = CLR_FEATURE,
		.wValue = feature,
		.wIndex = port,
		.wLength = 0
	};
	return r;
}

static inline struct usbreq __clear_hub_feature_req(uint16_t feature)
{
	struct usbreq r = {
		.bmRequestType = (USB_DIR_OUT | USB_TYPE_CLS | USB_RCPT_DEVICE),
		.bRequest = CLR_FEATURE,
		.wValue = feature,
		.wIndex = 0,
		.wLength = 0
	};
	return r;
}

static inline struct usbreq
__set_port_feature_req(uint16_t port, uint16_t feature)
{
	struct usbreq r = {
		.bmRequestType = (USB_DIR_OUT | USB_TYPE_CLS | USB_RCPT_OTHER),
		.bRequest = SET_FEATURE,
		.wValue = feature,
		.wIndex = port,
		.wLength = 0
	};
	return r;
}

static inline struct usbreq __set_hub_feature_req(uint16_t feature)
{
	struct usbreq r = {
		.bmRequestType = (USB_DIR_OUT | USB_TYPE_CLS | USB_RCPT_DEVICE),
		.bRequest = SET_FEATURE,
		.wValue = feature,
		.wIndex = 0,
		.wLength = 0
	};
	return r;
}

static inline struct usbreq __get_port_status_req(uint16_t port)
{
	struct usbreq r = {
		.bmRequestType = (USB_DIR_IN | USB_TYPE_CLS | USB_RCPT_OTHER),
		.bRequest = GET_STATUS,
		.wValue = 0,
		.wIndex = port,
		.wLength = 4
	};
	return r;
}

static inline struct usbreq __get_hub_status_req(void)
{
	struct usbreq r = {
		.bmRequestType = (USB_DIR_IN | USB_TYPE_CLS | USB_RCPT_DEVICE),
		.bRequest = GET_STATUS,
		.wValue = 0,
		.wIndex = 0,
		.wLength = 4
	};
	return r;
}

static inline struct usbreq __get_hub_descriptor_req(void)
{
	struct usbreq r = {
		.bmRequestType = (USB_DIR_IN | USB_TYPE_CLS | USB_RCPT_DEVICE),
		.bRequest = GET_DESCRIPTOR,
		.wValue = DESCRIPTOR_TYPE_HUB << 8,
		.wIndex = 0,
		.wLength = sizeof(struct hub_desc)
	};
	return r;
}

static inline struct usbreq __set_hub_descriptor_req(void)
{
	struct usbreq r = {
		.bmRequestType = (USB_DIR_OUT | USB_TYPE_CLS | USB_RCPT_DEVICE),
		.bRequest = SET_DESCRIPTOR,
		.wValue = DESCRIPTOR_TYPE_HUB << 8,
		.wIndex = 0,
		.wLength = sizeof(struct hub_desc)
	};
	return r;
}

struct usb_hubem {
	int hubem_nports;
	int pwr_delay_ms;
	int (*set_pf) (void *token, int port, enum port_feature feature);
	int (*clr_pf) (void *token, int port, enum port_feature feature);
	int (*get_pstat) (void *token, int port, struct port_status *ps);
	void *token;
};

static void _handle_port_change(usb_hub_t h, int port)
{
	struct xact xact[2];
	struct usbreq *req;
	struct port_status *sts;
	uint16_t change, status;
	int ret;

	if (!h) {
		ZF_LOGF("Invalid HUB\n");
	}

	if (!port) {
		ZF_LOGD(h, "Error: check hub status!\n");
		return;
	}

	ZF_LOGD(h, "Handle status change of port %d\n", port);

	/* Get port status change */
	xact[0].type = PID_SETUP;
	xact[0].len = sizeof(struct usbreq);
	xact[1].type = PID_IN;
	xact[1].len = sizeof(struct port_status);

	ret = usb_alloc_xact(h->udev->dman, xact, 2);
	if (ret) {
		ZF_LOGF("Out of DMA memory\n");
		abort();
	}
	req = xact_get_vaddr(&xact[0]);
	sts = xact_get_vaddr(&xact[1]);

	*req = __get_port_status_req(port);
	ret = usbdev_schedule_xact(h->udev, h->udev->ep_ctrl,
				   xact, 2, NULL, NULL);
	if (ret < 0) {
		ZF_LOGF("Transaction error\n");
		abort();
	}

	/* Cache the port status, because we need to clear it right away. */
	change = sts->wPortChange;
	status = sts->wPortStatus;

	ZF_LOGD(h, "Status change (0x%x:0x%x) on port %d.\n",
		change, status, port);

	/* Attach and detach detect event */
	if (change & BIT(PORT_CONNECTION)) {
		/* Clear the port connection status */
		*req = __clear_port_feature_req(port, C_PORT_CONNECTION);
		ret = usbdev_schedule_xact(h->udev, h->udev->ep_ctrl,
					   xact, 1, NULL, NULL);
		if (ret < 0) {
			ZF_LOGF("Transaction error\n");
			abort();
		}

		if (status & BIT(PORT_CONNECTION)) {
			ZF_LOGD(h, "Port %d connected\n", port);
			/* Wait for the device to stabilize, USB spec 9.1.2 */
			msdelay(100);

			/* Enable the connection by resetting the port */
			*req = __set_port_feature_req(port, PORT_RESET);
			ret = usbdev_schedule_xact(h->udev, h->udev->ep_ctrl,
						   xact, 1, NULL, NULL);
			if (ret < 0) {
				ZF_LOGF("Transaction error\n");
				abort();
			}

			/*
			 * Wait for the hub to exit the resetting state, refer
			 * to USB spec 11.5.1.5
			 * We also need to re-read the port status, it's updated
			 * by the reset.
			 */
			*req = __get_port_status_req(port);
			do {
				msdelay(10);
				ret =
				    usbdev_schedule_xact(h->udev,
							 h->udev->ep_ctrl, xact,
							 2, NULL, NULL);
				if (ret < 0) {
					ZF_LOGF("Transaction error\n");
					abort();
				}

				status = sts->wPortStatus;
			} while (status & BIT(PORT_RESET));

			/* Reset finished, clear reset status */
			*req = __clear_port_feature_req(port, C_PORT_RESET);
			ret = usbdev_schedule_xact(h->udev, h->udev->ep_ctrl,
						   xact, 1, NULL, NULL);
			if (ret < 0) {
				ZF_LOGF("Transaction error\n");
				abort();
			}

			/* Create the new device */
			enum usb_speed speed;
			usb_dev_t new_dev = NULL;
			usb_hub_t new_hub = NULL;

			if (status & BIT(PORT_HIGH_SPEED)) {
				speed = USBSPEED_HIGH;
			} else if (status & BIT(PORT_LOW_SPEED)) {
				speed = USBSPEED_LOW;
			} else {
				speed = USBSPEED_FULL;
			}

			ret = usb_new_device(h->udev, port, speed, &new_dev);
			if (ret < 0) {
				*req = __set_port_feature_req(port, PORT_RESET);
				ret =
				    usbdev_schedule_xact(h->udev,
							 h->udev->ep_ctrl, xact,
							 1, NULL, NULL);
				if (ret < 0) {
					ZF_LOGF("Transaction error\n");
					abort();
				}
				if (new_dev) {
					usbdev_disconnect(new_dev);
				}
			} else {
				h->port[port - 1].udev = new_dev;
				usb_hub_driver_bind(new_dev, &new_hub);
			}
		} else {
			ZF_LOGD(h, "Port %d disconnected\n", port);
			*req = __set_port_feature_req(port, PORT_SUSPEND);
			ret = usbdev_schedule_xact(h->udev, h->udev->ep_ctrl,
						   xact, 1, NULL, NULL);
			if (ret < 0) {
				ZF_LOGF("Transaction error\n");
				abort();
			}
			if (h->port[port - 1].udev) {
				usbdev_disconnect(h->port[port - 1].udev);
				h->port[port - 1].udev = NULL;
			}
		}
	}

	/* Port enable */
	if (change & BIT(PORT_ENABLE)) {
		ZF_LOGD(h, "Port %d enabled\n", port);
		/* Clear the port connection status */
		*req = __clear_port_feature_req(port, C_PORT_CONNECTION);
		ret = usbdev_schedule_xact(h->udev, h->udev->ep_ctrl,
					   xact, 1, NULL, NULL);
		if (ret < 0) {
			ZF_LOGF("Transaction error\n");
			abort();
		}
	}

	/* Port suspend */
	if (change & BIT(PORT_SUSPEND)) {
		ZF_LOGD(h, "Port %d suspended\n", port);
		/* Clear suspend status */
		*req = __clear_port_feature_req(port, C_PORT_SUSPEND);
		ret = usbdev_schedule_xact(h->udev, h->udev->ep_ctrl,
					   xact, 1, NULL, NULL);
		if (ret < 0) {
			ZF_LOGF("Transaction error\n");
			abort();
		}
	}

	/* Port over-current */
	if (change & BIT(PORT_OVER_CURRENT)) {
		ZF_LOGD(h, "Port %d over-current\n", port);
		/* Clear over-current status */
		*req = __clear_port_feature_req(port, C_PORT_OVER_CURRENT);
		ret = usbdev_schedule_xact(h->udev, h->udev->ep_ctrl,
					   xact, 1, NULL, NULL);
		if (ret < 0) {
			ZF_LOGF("Transaction error\n");
			abort();
		}
	}

	/* Port reset */
	if (change & BIT(PORT_RESET)) {
		ZF_LOGD(h, "Port %d reset\n", port);
		/* Clear reset status */
		*req = __clear_port_feature_req(port, C_PORT_RESET);
		ret = usbdev_schedule_xact(h->udev, h->udev->ep_ctrl,
					   xact, 1, NULL, NULL);
		if (ret < 0) {
			ZF_LOGF("Transaction error\n");
			abort();
		}
	}

	usb_destroy_xact(h->udev->dman, xact, 2);
}

static int
hub_irq_handler(void *token, enum usb_xact_status stat, int bytes_remaining)
{
	usb_hub_t h = (usb_hub_t) token;
	int i, j;
	int handled = 0;
	uint8_t *intbm;
	int len = h->int_xact.len - bytes_remaining;

	/* Check the status */
	if (stat != XACTSTAT_SUCCESS) {
		ZF_LOGD(h, "Received unsuccessful IRQ\n");
		return 1;
	}

	ZF_LOGD(h, "Handling IRQ\n");

	intbm = h->intbm;
	if (intbm != xact_get_vaddr(&h->int_xact)) {
		ZF_LOGF("Invalid bitmap\n");
		abort();
	}
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
		ZF_LOGD(h, "Spurious IRQ\n");
	}

	usbdev_schedule_xact(h->udev, h->udev->ep[0],
			     &h->int_xact, 1, &hub_irq_handler, h);
	return 0;
}

static int hub_config_cb(void *token, int cfg, int iface, struct anon_desc *d)
{
	struct endpoint_desc *e;
	usb_hub_t hub = (usb_hub_t)token;

	if (!hub) {
		ZF_LOGF("Invalid token\n");
	}
	if (d) {
		switch (d->bDescriptorType) {
		case ENDPOINT:
			e = (struct endpoint_desc*)d;
			/* We just take the first endpoint */
			hub->int_ep = e->bEndpointAddress & 0xf;
			hub->int_max_pkt = e->wMaxPacketSize;
			hub->int_rate_ms = e->bInterval * 2;
			hub->ifno = iface;
			hub->cfgno = cfg;
			break;
		default:
			/* Don't care */
			break;
		}
		return 0;
	} else {
		return 0;
	}
}

int usb_hub_driver_bind(usb_dev_t udev, usb_hub_t *hub)
{
	usb_hub_t h;
	struct usbreq *req;
	struct hub_desc *hdesc;
	struct xact xact[2];
	int err;
	int i;

	/* Check the class */
	if (usbdev_get_class(udev) != USB_CLASS_HUB) {
		return -1;
	}

	/* Allocate memory */
	h = (usb_hub_t) usb_malloc(sizeof(*h));
	if (h == NULL) {
		return -2;
	}
	memset(h, 0, sizeof(*h));
	h->udev = udev;
	udev->dev_data = (struct udev_priv *)h;

	/* Get hub descriptor for nports and power delay */
	ZF_LOGD(h, "Get hub descriptor\n");
	xact[0].type = PID_SETUP;
	xact[0].len = sizeof(*req);
	xact[1].type = PID_IN;
	xact[1].len = sizeof(*hdesc);
	err = usb_alloc_xact(udev->dman, xact, 2);
	if (err) {
		ZF_LOGF("Out of DMA memory\n");
		abort();
	}
	req = xact_get_vaddr(&xact[0]);
	*req = __get_hub_descriptor_req();
	err = usbdev_schedule_xact(udev, h->udev->ep_ctrl, xact, 2, NULL, NULL);
	if (err < 0) {
		usb_destroy_xact(udev->dman, xact, 2);
		usb_free(h);
		h = NULL;
		return -1;
	}
	hdesc = xact_get_vaddr(&xact[1]);
	h->nports = hdesc->bNbrPorts;
	h->power_good_delay_ms = hdesc->bPwrOn2PwrGood * 2;
	usb_destroy_xact(udev->dman, xact, 2);
	h->port = (struct usb_hub_port *)usb_malloc(sizeof(*h->port) * h->nports);
	if (!h->port) {
		ZF_LOGF("Out of memory\n");
		abort();
	}
	memset(h->port, 0, sizeof(*h->port) * h->nports);
	ZF_LOGD(h, "Parsing config\n");
	h->int_ep = -1;
	err = usbdev_parse_config(h->udev, &hub_config_cb, h);
	if (err || h->int_ep == -1) {
		usb_free(h);
		h = NULL;
		return -1;
	}
	ZF_LOGD(h, "Configure HUB\n");
	xact[0].type = PID_SETUP;
	xact[0].len = sizeof(*req);

	err = usb_alloc_xact(h->udev->dman, xact, 1);
	if (err) {
		ZF_LOGE("Out of DMA memory\n");
		return -1;
	}
	req = xact_get_vaddr(&xact[0]);
	*req = __set_configuration_req(h->cfgno);

	err = usbdev_schedule_xact(udev, h->udev->ep_ctrl, xact, 1, NULL, NULL);
	if (err < 0) {
		usb_destroy_xact(udev->dman, xact, 1);
		usb_free(h);
		h = NULL;
		return -1;
	}
	usb_destroy_xact(udev->dman, xact, 1);

	/* Power up ports */
	xact[0].type = PID_SETUP;
	xact[0].len = sizeof(*req);

	usb_alloc_xact(h->udev->dman, xact, 1);
	req = xact_get_vaddr(&xact[0]);
	for (i = 1; i <= h->nports; i++) {
		ZF_LOGD(h, "Power on port %d\n", i);
		*req = __set_port_feature_req(i, PORT_POWER);
		err = usbdev_schedule_xact(h->udev, h->udev->ep_ctrl,
					   xact, 1, NULL, NULL);
		if (err < 0) {
			ZF_LOGF("Transaction error\n");
			abort();
		}
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
	if (err) {
		ZF_LOGF("Out of DMA memory\n");
		abort();
	}
	h->intbm = xact_get_vaddr(&h->int_xact);
	ZF_LOGD(h, "Registering for INT\n");
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
	.bMaxPacketSize0 = 64,
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
	    sizeof(_hub_iface_desc) + sizeof(_hub_endpoint_desc),
	.bNumInterfaces = 1,
	.bConfigurationValue = 1,
	.iConfigurationIndex = 0,
	.bmAttributes = (1 << 7),
	.bMaxPower = 100 /*mA */  / 2
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
hubem_get_descriptor(usb_hubem_t dev, struct usbreq *req, void *buf, int len)
{
	int act_len = 0;
	int dtype = req->wValue >> 8;
	switch (dtype) {
	case DEVICE:{
		struct device_desc *ret = (struct device_desc *)buf;
		ZF_LOGD("Get device descriptor\n");
		act_len = MIN(len, sizeof(*ret));
		memcpy(ret, &_hub_device_desc, act_len);
		return act_len;}
	case DESCRIPTOR_TYPE_HUB:{
		struct hub_desc *ret = (struct hub_desc *)buf;
		int nregs = (dev->hubem_nports + 7) / 8;
		int i;
		ZF_LOGD("Get hub type descriptor\n");
		_hub_hub_desc.bNbrPorts = dev->hubem_nports;
		_hub_hub_desc.bPwrOn2PwrGood = dev->pwr_delay_ms / 2;
		_hub_hub_desc.bDescLength = 7 + nregs * 2;
		for (i = 0; i < nregs; i++) {
			_hub_hub_desc.portcfg[i] = 0;
			_hub_hub_desc.portcfg[i + nregs] = 0;
		}
		act_len = MIN(_hub_hub_desc.bDescLength, len);
		memcpy(ret, &_hub_hub_desc, act_len);
		return act_len;}
	case CONFIGURATION:{
		int cp_len;
		int pos = 0;
		int act_len;
		ZF_LOGD("Get configuration descriptor\n");
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
		_hub_endpoint_desc.wMaxPacketSize =
		    (dev->hubem_nports + 7) / 8;
		cp_len = MIN(act_len - pos, _hub_endpoint_desc.bLength);
		memcpy(buf + pos, &_hub_endpoint_desc, cp_len);
		pos += cp_len;
		if (pos != act_len) {
			ZF_LOGF("Invalid descriptor\n");
			abort();
		}
		return act_len;}
	case INTERFACE:{
		int act_len;
		ZF_LOGD("Get interface descriptor\n");
		act_len = MIN(_hub_iface_desc.bLength, len);
		memcpy(buf, &_hub_iface_desc, act_len);
		return act_len;}
	case ENDPOINT:{
		int act_len;
		ZF_LOGD("Get endpoint descriptor\n");
		act_len = MIN(_hub_endpoint_desc.bLength, len);
		memcpy(buf, &_hub_endpoint_desc, act_len);
		return act_len;}
	case STRING:
	case DEVICE_QUALIFIER:
	case OTHER_SPEED_CONFIGURATION:
	case INTERFACE_POWER:
	default:
		ZF_LOGD("Descriptor 0x%x not supported\n", dtype);
		return -1;
	}
}

static int hubem_feature(usb_hubem_t dev, struct usbreq *req)
{
	int f = req->wValue;
	int p = req->wIndex;
	void *t = dev->token;
	int ret;
	switch (req->bRequest) {
	case SET_FEATURE:
		ZF_LOGD("Set feature %d -> port %d\n", f, p);
		return dev->set_pf(t, p, f);
	case CLR_FEATURE:
		ZF_LOGD("Clear feature %d -> port %d\n", f, p);
		return dev->clr_pf(t, p, f);
	default:
		printf("Unsupported feature: %d\n", f);
		return -1;
	}
	return ret;
}

static int
hubem_get_status(usb_hubem_t dev, struct usbreq *req, void *buf, int len)
{
	int port = req->wIndex;
	if (port == 0) {
		/* Device status: self powered | remote wakeup */
		uint16_t stat = 0;
		int act_len;
		ZF_LOGD("Get Status: Device status\n");
		act_len = MIN(len, sizeof(stat));
		memcpy(buf, &stat, act_len);
		return act_len;
	} else if (port <= dev->hubem_nports) {
		/* Port status */
		struct port_status *psts = (struct port_status *)buf;
		int act_len = MIN(len, sizeof(*psts));
		if (len < sizeof(*psts)) {
			ZF_LOGF("Invalid port status\n");
			abort();
		}
		if (dev->get_pstat(dev->token, port, psts)) {
			ZF_LOGD
			    ("Get Status: Failed to read status for port %d\n",
			     port);
			return -1;
		} else {
			ZF_LOGD("Get Status: Success s0x%x c0x%0x on port %d\n",
			       psts->wPortStatus, psts->wPortChange, port);
			return act_len;
		}
	} else {
		ZF_LOGD("Get Status: Invalid port (%d/%d)\n", port,
		       dev->hubem_nports);
		return -1;
	}
}

int
hubem_process_xact(usb_hubem_t dev, struct xact *xact, int nxact,
		   usb_cb_t cb, void *t)
{
	struct usbreq *req;
	void *buf;
	int buf_len;
	int i;
	int err;

	for (err = 0, i = 0; !err && i < nxact; i++) {
		if (xact[i].type != PID_SETUP) {
			continue;
		}
		req = xact_get_vaddr(&xact[i]);
		if (xact[i].len < sizeof(*req)) {
			ZF_LOGF("Buffer too small\n");
			abort();
		}

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
			ZF_LOGD("Unhandled transaction: SET_CONFIGURATION\n");
			break;
		case SET_INTERFACE:
			ZF_LOGD("Unhandled transaction: SET_INTERFACE\n");
			break;
		case SET_ADDRESS:
			ZF_LOGD("Unhandled transaction: SET_ADDRESS\n");
			break;
		case CLR_FEATURE:
		case SET_FEATURE:
			err = hubem_feature(dev, req);
			break;
		default:
			ZF_LOGE("Request code %d not supported\n",
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
usb_hubem_driver_init(void *token, int nports, int pwr_delay_ms,
		      int (*set_pf) (void *token, int port,
				     enum port_feature feature),
		      int (*clr_pf) (void *token, int port,
				     enum port_feature feature),
		      int (*get_pstat) (void *token, int port,
					struct port_status *ps),
		      usb_hubem_t *hub)
{

	usb_hubem_t h;
	h = (usb_hubem_t) usb_malloc(sizeof(*h));
	if (h == NULL) {
		ZF_LOGE("Out of memory\n");
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
