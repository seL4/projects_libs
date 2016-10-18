/*
 * Copyright 2015, NICTA
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(NICTA_BSD)
 */

#ifndef _USB_STORAGE_H_
#define _USB_STORAGE_H_

#include <usb/usb.h>

int usb_storage_bind(usb_dev_t usb_dev);

int usb_storage_init_disk(usb_dev_t usb_dev);
uint32_t usb_storage_get_capacity(usb_dev_t usb_dev);
int usb_storage_write(usb_dev_t usb_dev, void *buf, int size);
int usb_storage_read(usb_dev_t usb_dev, void *buf, int size);
#endif /* _USB_STORAGE_H_ */

