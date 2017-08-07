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

/**
 * @brief USB CDC ACM driver
 * @see USB CDC specification and CDC PSTN subclass specification
 */
#include <stdio.h>
#include <string.h>

#include <utils/circular_buffer.h>
#include <platsupport/sync/atomic.h>
#include "../services.h"
#include "cdc.h"

/*
 * XXX: In theory the maximum xact size can be up to 20K, however, our DMA
 * allocator cannot handle that large allocation at the moment.
 */
#define CDC_READ_XACT_SIZE  128
#define CDC_READ_BUFFER_SIZE 4096

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
	"NCM Functional",
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
	struct usb_dev *udev;	 //The handle to the underlying USB device
	uint8_t subclass;        //Subclass code
	uint8_t config;          //Active configuration
	uint8_t comm;            //Communication interface index
	uint8_t data;            //Data interface index
	struct endpoint *ep_int; //Interrupt endpoint
	struct endpoint *ep_in;	 //BULK in endpoint
	struct endpoint *ep_out; //BULK out endpoint
	struct xact read_xact;   //Current read request
	circ_buf_t read_buf;     //Read buffer
	int read_in_progress;
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
			ZF_LOGD("Communication Interface\n");
			if (cdc->subclass < 0xd) {
				ZF_LOGD("  |-- %s\n", subclass_codes[cdc->subclass]);
			}
		} else if (cdc->udev->class == INF_DATA) {
			cdc->data = idesc->bInterfaceNumber;
			ZF_LOGD("Data Interface\n");
		}
		break;
	case CS_INTERFACE:
		fdesc = (struct func_desc *)desc;
		if (fdesc->bDescriptorSubtype < 0x1d) {
			ZF_LOGD("  %s\n",
					func_subtype_codes[fdesc->bDescriptorSubtype]);
		} else {
			ZF_LOGD("  Function type reserved(%x)\n",
					fdesc->bDescriptorSubtype);
		}
		break;
	default:
		break;
	}

	return 0;
}

static int usb_cdc_read_cb(void *token, enum usb_xact_status stat, int rbytes)
{
	struct usb_dev *udev;
	struct usb_cdc_device *cdc;
	char *buf;

	udev = (struct usb_dev*)token;
	cdc = (struct usb_cdc_device*)udev->dev_data;

	if (stat == XACTSTAT_SUCCESS) {
		buf = (char*)xact_get_vaddr(&cdc->read_xact);
		for (int i = 0; i < CDC_READ_XACT_SIZE - rbytes; i++) {
			if (!circ_buf_is_full(&cdc->read_buf)) {
				circ_buf_put(&cdc->read_buf, buf[i]);
			} else {
				break;
			}
		}
	}

	sync_atomic_decrement(&cdc->read_in_progress, __ATOMIC_RELAXED);

	return 0;
}

int usb_cdc_bind(usb_dev_t *udev)
{
	int err;
	struct usb_cdc_device *cdc;
	struct xact xact;
	struct usbreq *req;
	int class;

	if (!udev) {
		ZF_LOGF("Invalid device\n");
	}

	cdc = usb_malloc(sizeof(struct usb_cdc_device));
	if (!cdc) {
		ZF_LOGD("Not enough memory!\n");
		return -1;
	}

	cdc->udev = udev;
	udev->dev_data = (struct udev_priv*)cdc;

	/* Parse the descriptors */
	err = usbdev_parse_config(udev, usb_cdc_config_cb, cdc);
	if (err) {
		ZF_LOGF("Invalid descriptors\n");
	}

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

	char *buf = usb_malloc(CDC_READ_BUFFER_SIZE);
	if (!buf) {
		ZF_LOGD("Failed to allocate circular buffer!\n");
		usb_free(cdc);
		return -1;
	}

	err = circ_buf_new(buf, CDC_READ_BUFFER_SIZE, &cdc->read_buf);
	if (err) {
		ZF_LOGD("Failed to allocate circular buffer!\n");
		usb_free(buf);
		usb_free(cdc);
		return -1;
	}

	class = usbdev_get_class(udev);
	if (class != USB_CLASS_CDCDATA && class != USB_CLASS_COMM) {
		ZF_LOGD("Not a CDC device(%d)\n", class);
		circ_buf_free(&cdc->read_buf);
		usb_free(buf);
		usb_free(cdc);
		return -1;
	}

	ZF_LOGD("USB CDC found, subclass(%x)\n", cdc->subclass);

	/* Allocate read request */
	cdc->read_xact.type = PID_IN;
	cdc->read_xact.len = CDC_READ_XACT_SIZE;
	err = usb_alloc_xact(udev->dman, &cdc->read_xact, 1);
	if (err) {
		ZF_LOGF("Out of DMA memory\n");
	}
	cdc->read_in_progress = 0;

	/* Activate configuration */
	xact.len = sizeof(struct usbreq);
	err = usb_alloc_xact(udev->dman, &xact, 1);
	if (err) {
		ZF_LOGF("Out of DMA memory\n");
	}

	/* Fill in the request */
	xact.type = PID_SETUP;
	req = xact_get_vaddr(&xact);
	*req = __set_configuration_req(cdc->config);

	/* Send the request to the host */
	err = usbdev_schedule_xact(udev, udev->ep_ctrl, &xact, 1, NULL, NULL);
	if (err) {
		ZF_LOGF("Transaction error\n");
	}
	usb_destroy_xact(udev->dman, &xact, 1);

	return 0;
}

int usb_cdc_read(usb_dev_t *udev, void *buf, int len)
{
	int err;
	int cnt = 0;
	struct usb_cdc_device *cdc;

	cdc = (struct usb_cdc_device*)udev->dev_data;

	if (!cdc->read_in_progress) {
		err = usbdev_schedule_xact(udev, cdc->ep_in, &cdc->read_xact, 1,
				usb_cdc_read_cb, udev);
		if (err) {
			ZF_LOGF("Transaction error\n");
		}
		sync_atomic_increment(&cdc->read_in_progress, __ATOMIC_RELAXED);
	}

	/* Get data from the read buffer */
	while (len--) {
		if (!circ_buf_is_empty(&cdc->read_buf)) {
			*((char*)buf + cnt) = circ_buf_get(&cdc->read_buf);
			cnt++;
		} else {
			break;
		}
	}

	return cnt;
}

int usb_cdc_write(usb_dev_t *udev, const void *buf, int len)
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
	if (!xact) {
		ZF_LOGF("Out of memory\n");
	}

	/* Fill in the length of each xact */
	for (int i = 0; i < cnt; i++) {
		xact[i].type = PID_OUT;
		xact[i].len = len < MAX_XACT_SIZE ? len : MAX_XACT_SIZE;
		len -= xact[i].len;
	}

	/* DMA allocation */
	err = usb_alloc_xact(udev->dman, xact, cnt);
	if (err) {
		ZF_LOGF("Out of DMA memory\n");
	}

	/* Copy in */
	offset = 0;
	for (int i = 0; i < cnt; i++) {
		memcpy(xact_get_vaddr(&xact[i]), (char*)buf + offset, xact[i].len);
		offset += xact[i].len;
	}

	/* Send to the host */
	err = usbdev_schedule_xact(udev, cdc->ep_out, xact, cnt, NULL, NULL);
	if (err) {
		ZF_LOGF("Transaction error\n");
	}

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
	if (err) {
		ZF_LOGF("Out of DMA memory\n");
	}

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
void cdc_send_encap_cmd(usb_dev_t *udev, const void *buf, int len)
{
	struct usb_cdc_device *cdc = (struct usb_cdc_device*)udev->dev_data;

	usb_cdc_mgmt_msg(cdc, USB_DIR_OUT | USB_TYPE_CLS | USB_RCPT_INTERFACE,
			SEND_ENCAPSULATED_COMMAND, 0, (void*)buf, len);
}

void cdc_get_encap_resp(usb_dev_t *udev, void *buf, int len)
{
	struct usb_cdc_device *cdc = (struct usb_cdc_device*)udev->dev_data;

	usb_cdc_mgmt_msg(cdc, USB_DIR_IN | USB_TYPE_CLS | USB_RCPT_INTERFACE,
			GET_ENCAPSULATED_RESPONSE, 0, buf, len);
}

/* PSTN - Abstract Control Model Requests */
void acm_set_comm_feature(usb_dev_t *udev, enum acm_comm_feature f,
		uint16_t state)
{
	struct usb_cdc_device *cdc = (struct usb_cdc_device*)udev->dev_data;

	usb_cdc_mgmt_msg(cdc, USB_DIR_OUT | USB_TYPE_CLS | USB_RCPT_INTERFACE,
			SET_COMM_FEATURE, f, &state, 2);
}

uint16_t acm_get_comm_feature(usb_dev_t *udev, enum acm_comm_feature f)
{
	uint16_t state;
	struct usb_cdc_device *cdc = (struct usb_cdc_device*)udev->dev_data;

	usb_cdc_mgmt_msg(cdc, USB_DIR_OUT | USB_TYPE_CLS | USB_RCPT_INTERFACE,
			GET_COMM_FEATURE, f, &state, 2);
	return state;
}

void acm_clear_comm_feature(usb_dev_t *udev, enum acm_comm_feature f)
{
	struct usb_cdc_device *cdc = (struct usb_cdc_device*)udev->dev_data;

	usb_cdc_mgmt_msg(cdc, USB_DIR_OUT | USB_TYPE_CLS | USB_RCPT_INTERFACE,
			CLEAR_COMM_FEATURE, f, NULL, 0);
}

void acm_set_line_coding(usb_dev_t *udev, const struct acm_line_coding *coding)
{
	struct usb_cdc_device *cdc = (struct usb_cdc_device*)udev->dev_data;

	usb_cdc_mgmt_msg(cdc, USB_DIR_OUT | USB_TYPE_CLS | USB_RCPT_INTERFACE,
			SET_LINE_CODING, 0, (struct acm_line_coding*)coding,
			sizeof(*coding));
}

void acm_get_line_coding(usb_dev_t *udev, struct acm_line_coding *coding)
{
	struct usb_cdc_device *cdc = (struct usb_cdc_device*)udev->dev_data;

	usb_cdc_mgmt_msg(cdc, USB_DIR_IN | USB_TYPE_CLS | USB_RCPT_INTERFACE,
			GET_LINE_CODING, 0, coding, sizeof(*coding));
}

void acm_set_ctrl_line_state(usb_dev_t *udev, uint8_t ctrl)
{
	struct usb_cdc_device *cdc = (struct usb_cdc_device*)udev->dev_data;

	usb_cdc_mgmt_msg(cdc, USB_DIR_OUT | USB_TYPE_CLS | USB_RCPT_INTERFACE,
			SET_CONTROL_LINE_STATE, ctrl, NULL, 0);
}

void acm_send_break(usb_dev_t *udev, uint16_t us)
{
	struct usb_cdc_device *cdc = (struct usb_cdc_device*)udev->dev_data;

	usb_cdc_mgmt_msg(cdc, USB_DIR_OUT | USB_TYPE_CLS | USB_RCPT_INTERFACE,
			SEND_BREAK, us, NULL, 0);
}

