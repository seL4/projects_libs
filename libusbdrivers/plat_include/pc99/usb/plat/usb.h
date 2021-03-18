/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef _PLAT_USB_H_
#define _PLAT_USB_H_

enum usb_host_id {
    USB_HOST1,
    USB_HOST2,
    USB_NHOSTS,
    USB_HOST_DEFAULT = USB_HOST2
};

enum usb_otg_id {
    USB_NOTGS,
    USB_OTG_DEFAULT = -1
};

#endif /* _PLAT_USB_H_ */
