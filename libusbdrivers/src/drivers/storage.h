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
#ifndef _DRIVERS_STORAGE_H_
#define _DRIVERS_STORAGE_H_

int usb_storage_bind(usb_dev_t udev);
int usb_storage_xfer(usb_dev_t udev, void *cb, size_t cb_len,
		 struct xact *data, int ndata, int direction);
#endif /* _DRIVERS_STORAGE_H_ */


