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

#ifndef _EHCI_EHCI_H_
#define _EHCI_EHCI_H_

#include <usb/usb_host.h>

/**
 * Initialise a ehci host controller
 * @param[in/out] hdev        A host controller structure to
 *                            populate. Must be prefilled with a
 *                            DMA allocator. This function will
 *                            fill the private data and function
 *                            pointers of this structure.
 * @param[in]     cap_regs    memory location of the mapped echi
 *                            capability registers
 * @param[int]    board_pwren Function to call when power on/off
 *                            a port. Generally the PHY will take
 *                            care of this, but in cases where there
 *                            is no PHY (HSIC), a GPIO, etc may need
 *                            to be manually contolled.
 * @return                    0 on success
 */
int ehci_host_init(usb_host_t* hdev, uintptr_t cap_regs,
                   void (*board_pwren)(int port, int state));



#endif /* _EHCI_EHCI_H_ */
