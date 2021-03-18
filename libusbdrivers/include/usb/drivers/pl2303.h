/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#ifndef _USB_PL2303_H_
#define _USB_PL2303_H_

#include <platsupport/serial.h>
#include <usb/usb.h>

int usb_pl2303_bind(usb_dev_t *usb_dev);

int usb_pl2303_configure(usb_dev_t *udev, uint32_t bps, uint8_t char_size,
		enum serial_parity parity, uint8_t stop);
int usb_pl2303_write(usb_dev_t *udev, void *buf, int len);
int usb_pl2303_read(usb_dev_t *udev, void *buf, int len);

#endif /* _USB_PL2303_H_ */

