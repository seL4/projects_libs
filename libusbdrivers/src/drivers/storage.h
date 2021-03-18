/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#ifndef _DRIVERS_STORAGE_H_
#define _DRIVERS_STORAGE_H_

#include <usb/usb.h>

int usb_storage_bind(struct usb_dev *udev);
int usb_storage_xfer(struct usb_dev *udev, void *cb, size_t cb_len,
		 struct xact *data, int ndata, int direction);
#endif /* _DRIVERS_STORAGE_H_ */


