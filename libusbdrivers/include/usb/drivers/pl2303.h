/*
 * Copyright 2016, NICTA
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(NICTA_BSD)
 */

#ifndef _USB_PL2303_H_
#define _USB_PL2303_H_

#include <platsupport/serial.h>
#include <usb/usb.h>

int usb_pl2303_bind(usb_dev_t usb_dev);

int usb_pl2303_configure(usb_dev_t udev, uint32_t bps, uint8_t char_size,
		enum serial_parity parity, uint8_t stop);
int usb_pl2303_write(usb_dev_t udev, void *buf, int len);
int usb_pl2303_write(usb_dev_t udev, void *buf, int len);

#endif /* _USB_PL2303_H_ */

