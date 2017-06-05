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

#ifndef _USB_OTG_H_
#define _USB_OTG_H_

#include <usb/usb.h>

struct usb_otg_dev;
typedef struct usb_otg_dev* usb_otg_t;

/** Return the default OTG device ID for the running platform
 * @return the default OTG device ID for the running platform.
 */
int usb_default_otg_id(void);

/** Return the number of OTG devices on the running platform
 * @return the number of OTG devices on the running platform
 */
int usb_notgs(void);

int usb_otg_init(int id, usb_otg_t* otg, ps_io_ops_t ioops);

/** Pass control to the devices IRQ handler
 * @param[in] host    The USB host that triggered
 *                    the interrupt event.
 */
void otg_handle_irq(usb_otg_t otg);



typedef void (*otg_setup_cb)(usb_otg_t otg,
                             void* token, struct usbreq* req);
/* NOTE: the "set address" request will be intercepted */
int otg_ep0_setup(usb_otg_t otg, otg_setup_cb cb, void* token);

typedef void (*otg_prime_cb)(usb_otg_t otg, void* token,
                             enum usb_xact_status stat);
int otg_prime(usb_otg_t otg, int ep, enum usb_xact_type dir,
              void* vbuf, uintptr_t pbuf, int len,
              otg_prime_cb cb, void* token);

#endif /* _USB_OTG_H_ */


