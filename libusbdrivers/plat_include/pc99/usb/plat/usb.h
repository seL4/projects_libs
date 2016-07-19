/*
 * Copyright 2014, NICTA
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(NICTA_BSD)
 */


#ifndef _PLAT_USB_H_
#define _PLAT_USB_H_

enum usb_host_id {
    USB_HOST1,
    USB_HOST2,
    USB_NHOSTS,
    USB_HOST_DEFAULT = USB_HOST1
};

enum usb_otg_id {
    USB_NOTGS,
    USB_OTG_DEFAULT = -1
};

#endif /* _PLAT_USB_H_ */
