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
/*
 * These functions need to be implemented by platform
 * specific code.
 */

#ifndef _PLAT_USBOTG_H_
#define _PLAT_USBOTG_H_
#include <usb/otg.h>

struct usb_otg_dev;
typedef struct usb_otg_dev* usb_otg_t;


struct usb_otg_dev {
    int id;
    ps_dma_man_t* dman;
    /* Operations */
    int (*ep0_setup)(usb_otg_t otg,
                     otg_setup_cb cb, void* token);
    int (*prime)(usb_otg_t otg, int ep, enum usb_xact_type dir,
                 void* vbuf, uintptr_t pbuf, int len,
                 otg_prime_cb cb, void* token);
    /* Platform private data */
    struct usb_otg_data* pdata;
};

/**
 * Initialise USB OTG controller
 * @param[in/out] odev   An OTG structure to populate. This must
 *                       already be filled with a DMA allocator.
 *                       and the device ID.
 * @param[in]     ioops  a list of io operation functions.
 *                       of the initialised host controller
 * @return               0 on success
 */
int usb_plat_otg_init(usb_otg_t odev, ps_io_ops_t* ioops);

/** TODO this is actually called from platform code and should
 * not be public.
 * Initialise a ehci OTG controller
 * @param[in/out] odev     A OTG controller structure to
 *                         populate. Must be pre-filled with a
 *                         DMA allocator. This function will
 *                         fill the private data and function
 *                         pointers of this structure.
 * @param[int]    cap_regs memory location of the mapped ECHI
 *                         capability registers
 * @return                 0 on success
 */
int ehci_otg_init(usb_otg_t odev, uintptr_t cap_regs);


/**
 * Returns the number of OTG controllers on the platform
 * @return the number of OTG controllers on the platform
 */
int usb_plat_notgs(void);

/**
 * Returns the default OTG ID for the running platform
 * @return the default OTG ID for the running platform
 */
int usb_plat_default_otg(void);

void otg_plat_handle_irq(usb_otg_t otg);

#endif /* _PLAT_USBOTG_H_ */
