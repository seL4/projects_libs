/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#ifndef _OTG_USBTTY_H_
#define _OTG_USBTTY_H_

#include <usb/otg.h>

struct otg_usbtty;
typedef struct otg_usbtty* otg_usbtty_t;

int otg_usbtty_init(usb_otg_t otg, ps_dma_man_t* dman, otg_usbtty_t* usbtty);

#endif /* _OTG_USBTTY_H_ */
