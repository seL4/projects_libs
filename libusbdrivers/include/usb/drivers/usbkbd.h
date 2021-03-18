/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#ifndef _USB_DRIVERS_USBKBD_H_
#define _USB_DRIVERS_USBKBD_H_

#include <usb/usb.h>
#include <platsupport/chardev.h>

/* USB keyboard */
int usb_kbd_driver_bind(usb_dev_t *usb_dev, struct ps_chardevice *cdev);

#endif /* _USB_DRIVERS_USBKBD_H_ */
