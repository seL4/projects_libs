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
#ifndef _USB_STORAGE_H_
#define _USB_STORAGE_H_

#include <usb/usb.h>

int ufi_init_disk(usb_dev_t usb_dev)
uint32_t ufi_read_capacity(usb_dev_t usb_dev);

#endif /* _USB_STORAGE_H_ */

