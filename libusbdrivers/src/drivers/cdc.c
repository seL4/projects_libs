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
	usb_dev_t udev;	  //The handle to the underlying USB device
	uint8_t subclass; //Subclass code
	uint8_t config;   //Active configuration
	uint8_t comm;     //Communication interface index
	uint8_t data;     //Data interface index
	struct endpoint *ep_int; //Interrupt endpoint
	struct endpoint *ep_in;	 //BULK in endpoint
	struct endpoint *ep_out; //BULK out endpoint
};

static int
usb_cdc_config_cb(void *token, int cfg, int iface, struct anon_desc *desc)
{
	struct usb_cdc_device *cdc;
	struct config_desc *cdesc;
	struct iface_desc *idesc;
	struct func_desc *fdesc;

	if (!desc) {
		return 0;
	}

	cdc = (struct usb_cdc_device *)token;

	switch (desc->bDescriptorType) {
	case CONFIGURATION:
		cdesc = (struct config_desc*)desc;
		cdc->config = cdesc->bConfigurationValue;
		break;
	case INTERFACE:
		idesc = (struct iface_desc *)desc;
		cdc->udev->class = idesc->bInterfaceClass;
		cdc->subclass = idesc->bInterfaceSubClass;
		if (cdc->udev->class == INF_COMM && cdc->subclass < 0xd) {
			cdc->comm = idesc->bInterfaceNumber;
			CDC_DBG("Communication Interface\n");
			if (cdc->subclass < 0xd) {
				CDC_DBG("  |-- %s\n", subclass_codes[cdc->subclass]);
			}
		} else if (cdc->udev->class == INF_DATA) {
			cdc->data = idesc->bInterfaceNumber;
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
	struct xact xact;
	struct usbreq *req;
	int class;

	assert(udev);

	cdc = usb_malloc(sizeof(struct usb_cdc_device));
	if (!cdc) {
		CDC_DBG("Not enough memory!\n");
		return -1;
	}

	cdc->udev = udev;
	udev->dev_data = (struct udev_priv*)cdc;

	/* Parse the descriptors */
	err = usbdev_parse_config(udev, usb_cdc_config_cb, cdc);
	assert(!err);

	/* Find endpoints */
	for (int i = 0; udev->ep[i] != NULL; i++) {
		if (udev->ep[i]->type == EP_BULK) {
			if (udev->ep[i]->dir == EP_DIR_OUT) {
				cdc->ep_out = udev->ep[i];
			} else {
				cdc->ep_in = udev->ep[i];
			}
		} else if (udev->ep[i]->type == EP_INTERRUPT) {
			cdc->ep_int = udev->ep[i];
		} else {
			continue;
		}
	}

	class = usbdev_get_class(udev);
	if (class != USB_CLASS_COMM) {
		CDC_DBG("Not a CDC device(%d)\n", class);
		return -1;
	}

	CDC_DBG("USB CDC found, subclass(%x)\n", cdc->subclass);

	/* Activate configuration */
	xact.len = sizeof(struct usbreq);
	err = usb_alloc_xact(udev->dman, &xact, 1);
	assert(!err);

	/* Fill in the request */
	xact.type = PID_SETUP;
	req = xact_get_vaddr(&xact);
	*req = __set_configuration_req(cdc->config);

	/* Send the request to the host */
	err = usbdev_schedule_xact(udev, udev->ep_ctrl, &xact, 1, NULL, NULL);
	assert(!err);
	usb_destroy_xact(udev->dman, &xact, 1);

	return 0;
}

int usb_cdc_read(usb_dev_t udev, void *buf, int len)
{
	int err;
	int cnt;
	int received;
	struct usb_cdc_device *cdc;
	struct xact *xact;

	cdc = (struct usb_cdc_device*)udev->dev_data;

	/* Xact needs to be virtually contiguous */
	cnt = ROUND_UP(len, MAX_XACT_SIZE) / MAX_XACT_SIZE;

	xact = usb_malloc(sizeof(struct xact) * cnt);

	/* Fill in the length of each xact */
	for (int i = 0; i < cnt; i++) {
		xact[i].type = PID_IN;
		xact[i].len = len < MAX_XACT_SIZE ? len : MAX_XACT_SIZE;
		len -= xact[i].len;
	}

	/* DMA allocation */
	err = usb_alloc_xact(udev->dman, xact, cnt);
	assert(!err);

	/* Send to the host */
	err = usbdev_schedule_xact(udev, cdc->ep_in, xact, cnt, NULL, NULL);
	assert(err >= 0);

	/*
	 * Copy out the received data
	 * TODO: Copy the actual number of bytes received only.
	 */
	received = 0;
	for (int i = 0; i < cnt; i++) {
		memcpy((char*)buf + received, xact_get_vaddr(&xact[i]), xact[i].len);
		received += xact[i].len;
	}

	/* Cleanup */
	usb_destroy_xact(udev->dman, xact, cnt);

	usb_free(xact);

	return received - err;
}

int usb_cdc_write(usb_dev_t udev, void *buf, int len)
{
	int err;
	int cnt;
	int offset;
	struct usb_cdc_device *cdc;
	struct xact *xact;

	cdc = (struct usb_cdc_device*)udev->dev_data;

	/* Xact needs to be virtually contiguous */
	cnt = ROUND_UP(len, MAX_XACT_SIZE) / MAX_XACT_SIZE;

	xact = usb_malloc(sizeof(struct xact) * cnt);

	/* Fill in the length of each xact */
	for (int i = 0; i < cnt; i++) {
		xact[i].type = PID_OUT;
		xact[i].len = len < MAX_XACT_SIZE ? len : MAX_XACT_SIZE;
		len -= xact[i].len;
	}

	/* DMA allocation */
	err = usb_alloc_xact(udev->dman, xact, cnt);
	assert(!err);

	/* Copy in */
	offset = 0;
	for (int i = 0; i < cnt; i++) {
		memcpy(xact_get_vaddr(&xact[i]), (char*)buf + offset, xact[i].len);
		offset += xact[i].len;
	}

	/* Send to the host */
	err = usbdev_schedule_xact(udev, cdc->ep_out, xact, cnt, NULL, NULL);
	assert(!err);

	/* Cleanup */
	usb_destroy_xact(udev->dman, xact, cnt);

	usb_free(xact);

	return len;
}

static void
usb_cdc_mgmt_msg(struct usb_cdc_device *cdc, uint8_t req_type,
		enum cdc_req_code code, int value, void *buf, int len)
{
	int err;
	struct usbreq *req;
	struct xact msg[2];
	int cnt;

	/* Allocate xact */
	msg[0].len = sizeof(struct usbreq);
	msg[1].len = len;
	err = usb_alloc_xact(cdc->udev->dman, msg, 2);
	assert(!err);

	/* Management element request */
	msg[0].type = PID_SETUP;
	req = xact_get_vaddr(&msg[0]);
	req->bmRequestType = req_type;
	req->bRequest = code;
	req->wValue = value;
	req->wIndex = cdc->comm;
	req->wLength = len;
	cnt = 1;

	/* Data stage */
	if (len > 0) {
		if (req_type & USB_DIR_IN) {
			msg[1].type = PID_IN;
		} else {
			msg[1].type = PID_OUT;
		}
		memcpy(xact_get_vaddr(&msg[1]), buf, len);
		cnt++;
	}

	/* Send to the host */
	err = usbdev_schedule_xact(cdc->udev, cdc->udev->ep_ctrl,
			msg, cnt, NULL, NULL);

	/* Copy out */
	if (len > 0 && msg[1].type == PID_IN) {
		memcpy(xact_get_vaddr(&msg[1]), buf, len);
	}

	/* Cleanup */
	usb_destroy_xact(cdc->udev->dman, msg, 2);
}

/* Communication Device Class Requests */
void cdc_send_encap_cmd(usb_dev_t udev, void *buf, int len)
{
	struct usb_cdc_device *cdc = (struct usb_cdc_device*)udev->dev_data;

	usb_cdc_mgmt_msg(cdc, USB_DIR_OUT | USB_TYPE_CLS | USB_RCPT_INTERFACE,
			SEND_ENCAPSULATED_COMMAND, 0, buf, len);
}

void cdc_get_encap_resp(usb_dev_t udev, void *buf, int len)
{
	struct usb_cdc_device *cdc = (struct usb_cdc_device*)udev->dev_data;

	usb_cdc_mgmt_msg(cdc, USB_DIR_IN | USB_TYPE_CLS | USB_RCPT_INTERFACE,
			GET_ENCAPSULATED_RESPONSE, 0, buf, len);
}

/* PSTN - Abstract Control Model Requests */
void acm_set_comm_feature(usb_dev_t udev, enum acm_comm_feature f,
		uint16_t state)
{
	struct usb_cdc_device *cdc = (struct usb_cdc_device*)udev->dev_data;

	usb_cdc_mgmt_msg(cdc, USB_DIR_OUT | USB_TYPE_CLS | USB_RCPT_INTERFACE,
			SET_COMM_FEATURE, f, &state, 2);
}

uint16_t acm_get_comm_feature(usb_dev_t udev, enum acm_comm_feature f)
{
	uint16_t state;
	struct usb_cdc_device *cdc = (struct usb_cdc_device*)udev->dev_data;

	usb_cdc_mgmt_msg(cdc, USB_DIR_OUT | USB_TYPE_CLS | USB_RCPT_INTERFACE,
			GET_COMM_FEATURE, f, &state, 2);
	return state;
}

void acm_clear_comm_feature(usb_dev_t udev, enum acm_comm_feature f)
{
	struct usb_cdc_device *cdc = (struct usb_cdc_device*)udev->dev_data;

	usb_cdc_mgmt_msg(cdc, USB_DIR_OUT | USB_TYPE_CLS | USB_RCPT_INTERFACE,
			CLEAR_COMM_FEATURE, f, NULL, 0);
}

void acm_set_line_coding(usb_dev_t udev, struct acm_line_coding *coding)
{
	struct usb_cdc_device *cdc = (struct usb_cdc_device*)udev->dev_data;

	usb_cdc_mgmt_msg(cdc, USB_DIR_OUT | USB_TYPE_CLS | USB_RCPT_INTERFACE,
			SET_LINE_CODING, 0, coding, sizeof(*coding));
}

void acm_get_line_coding(usb_dev_t udev, struct acm_line_coding *coding)
{
	struct usb_cdc_device *cdc = (struct usb_cdc_device*)udev->dev_data;

	usb_cdc_mgmt_msg(cdc, USB_DIR_IN | USB_TYPE_CLS | USB_RCPT_INTERFACE,
			GET_LINE_CODING, 0, coding, sizeof(*coding));
}

void acm_set_ctrl_line_state(usb_dev_t udev, uint8_t ctrl)
{
	struct usb_cdc_device *cdc = (struct usb_cdc_device*)udev->dev_data;

	usb_cdc_mgmt_msg(cdc, USB_DIR_OUT | USB_TYPE_CLS | USB_RCPT_INTERFACE,
			SET_CONTROL_LINE_STATE, ctrl, NULL, 0);
}

void acm_send_break(usb_dev_t udev, uint16_t us)
{
	struct usb_cdc_device *cdc = (struct usb_cdc_device*)udev->dev_data;

	usb_cdc_mgmt_msg(cdc, USB_DIR_OUT | USB_TYPE_CLS | USB_RCPT_INTERFACE,
			SEND_BREAK, us, NULL, 0);
}

