/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#ifndef _USB_STORAGE_H_
#define _USB_STORAGE_H_

#include <usb/usb.h>

int ufi_init_disk(usb_dev_t *usb_dev);
uint32_t ufi_read_capacity(usb_dev_t *usb_dev);

#endif /* _USB_STORAGE_H_ */

