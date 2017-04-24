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

#include <usb/otg.h>
#include "plat/usb_otg.h"
#include "services.h"
#include <stdio.h>

int usb_otg_init(int id, usb_otg_t * otg_ptr, ps_io_ops_t ioops)
{
	usb_otg_t otg;
	int err;
	/* Validate the id */
	if (id < 0 || id > USB_NOTGS) {
		return -1;
	}
	/* Allocate host memory */
	otg = usb_malloc(sizeof(*otg));
	if (otg == NULL) {
		assert(0);
		return -1;
	}
	otg->dman = &ioops.dma_manager;
	otg->id = id;
	otg->ep0_setup = NULL;
	otg->prime = NULL;
	err = usb_plat_otg_init(otg, &ioops);
	if (!err) {
		*otg_ptr = otg;
	}
	return err;
}

void otg_handle_irq(usb_otg_t otg)
{
	otg_plat_handle_irq(otg);
}

int otg_ep0_setup(usb_otg_t otg, otg_setup_cb cb, void *token)
{
	assert(otg);
	assert(otg->ep0_setup);
	return otg->ep0_setup(otg, cb, token);
}

int
otg_prime(usb_otg_t otg, int ep, enum usb_xact_type dir,
	  void *vbuf, uintptr_t pbuf, int len, otg_prime_cb cb, void *token)
{
	return otg->prime(otg, ep, dir, vbuf, pbuf, len, cb, token);
}
