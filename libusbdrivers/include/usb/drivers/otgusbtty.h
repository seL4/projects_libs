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

#ifndef _OTG_USBTTY_H_
#define _OTG_USBTTY_H_

#include <usb/otg.h>

struct otg_usbtty;
typedef struct otg_usbtty* otg_usbtty_t;

int otg_usbtty_init(usb_otg_t otg, ps_dma_man_t* dman, otg_usbtty_t* usbtty);

#endif /* _OTG_USBTTY_H_ */
