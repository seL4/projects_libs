/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#ifndef _HID_H_
#define _HID_H_

#include <usb/usb.h>

/*
 * USB Human Input Device
 *
 * XXX: Assuming one report descriptor per interface; Physical descriptors not
 * supported.
 */
struct usb_hid_device {
	struct usb_dev *udev; //The handle to the underlying USB device
	uint8_t subclass;     //Subclass code(0 -- no subclass, 1 -- boot interface)
	uint8_t protocol;     //protocol code(1 -- keyboard, 2 -- mouse)
	uint8_t config;       //Active configuration
	uint8_t iface;        //Active interface
	uint16_t report_size; //Report descriptor size
};

enum hid_req_code {
	GET_REPORT   = 0x1,  //Mandatory for all devices
	GET_IDLE     = 0x2,
	GET_PROTOCOL = 0x3,  //Boot devices only
	SET_REPORT   = 0x9,
	SET_IDLE     = 0xA,
	SET_PROTOCOL = 0xB   //Boot devices only
};

enum hid_report_type {
	REPORT_INPUT   = 0x1,
	REPORT_OUTPUT  = 0x2,
	REPORT_FEATURE = 0x3
};

enum hid_protocol {
	HID_PROT_BOOT   = 0x0,
	HID_PORT_REPORT = 0x1
};

struct usb_hid_device *usb_hid_alloc(struct usb_dev *usb_dev);
void usb_hid_free(struct usb_hid_device *hid);

int usb_hid_set_idle(struct usb_hid_device *hid, int idle_ms);
int usb_hid_get_idle(struct usb_hid_device *hid);
int usb_hid_get_report(struct usb_hid_device *hid, enum hid_report_type type, void *buf);
int usb_hid_set_report(struct usb_hid_device *hid, enum hid_report_type type,
		void *buf, int size);
int usb_hid_get_protocol(struct usb_hid_device *hid);
int usb_hid_set_protocol(struct usb_hid_device *hid);

#endif //_HID_H_
