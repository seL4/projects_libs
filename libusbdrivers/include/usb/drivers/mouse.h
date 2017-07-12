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
#ifndef _USB_DRIVERS_MOUSE_H_
#define _USB_DRIVERS_MOUSE_H_

#include <usb/usb.h>
#include <platsupport/chardev.h>

/* USB mouse */
struct mouse_event {
	uint8_t button;
	uint8_t x;
	uint8_t y;
} __attribute__((packed));

int usb_mouse_driver_bind(usb_dev_t usb_dev, struct ps_chardevice *cdev);

#endif /* _USB_DRIVERS_MOUSE_H_ */
