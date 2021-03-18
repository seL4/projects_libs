/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include <usb/usb.h>
#include <autoconf.h>
#include <usbdrivers/gen_config.h>
#include <lwip/gen_config.h>

#define LAN9730_PID  0x9730
#define LAN9730_VID  0x0424

static inline int is_eth(usb_dev_t *dev)
{
    return dev->prod_id == LAN9730_PID && dev->vend_id == LAN9730_VID;
}


#ifdef CONFIG_LIB_LWIP
#include <lwip/netif.h>
struct netif *lan9730_driver_bind(usb_dev_t *udev);

int lan9730_input(struct netif *netif);

int lan9730_poll_status(struct netif *netif);
#endif /* CONFIG_LIB_LWIP */
