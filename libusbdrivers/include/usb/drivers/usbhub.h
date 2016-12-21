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

#ifndef _USB_DRIVERS_USBHUB_H_
#define _USB_DRIVERS_USBHUB_H_

#include <usb/usb.h>

/* USB hub */

struct hub_status {
    uint16_t wHubStatus;
    uint16_t wHubChange;
} __attribute__ ((packed));

struct port_status {
    uint16_t wPortStatus;
    uint16_t wPortChange;
} __attribute__ ((packed));

/* hub feature selectors defined by HUB spec */
enum hub_feature {
    C_HUB_LOCAL_POWER     = 0,
    C_HUB_OVER_CURRENT    = 1
};

/* port feature selectors defined by HUB spec */
enum port_feature {
    PORT_CONNECTION       = 0,
    PORT_ENABLE           = 1,
    PORT_SUSPEND          = 2,
    PORT_OVER_CURRENT     = 3,
    PORT_RESET            = 4,
    PORT_POWER            = 8,
    PORT_LOW_SPEED        = 9,
    PORT_HIGH_SPEED       = 10,
    /* These only used when clearing status changes */
    C_PORT_CONNECTION     = 16,
    C_PORT_ENABLE         = 17,
    C_PORT_SUSPEND        = 18,
    C_PORT_OVER_CURRENT   = 19,
    C_PORT_RESET          = 20,
    PORT_TEST             = 21,
    PORT_INDICATOR        = 22
};

struct usb_hub_port {
    struct usb_dev* udev;
};

struct usb_hub {
    usb_dev_t udev;
/// Configuration parameters
    struct endpoint *ep_int;
    int ifno, cfgno, int_ep, int_max_pkt, int_rate_ms;
/// Port book keeping
    int nports;
    struct usb_hub_port* port;
    int power_good_delay_ms;
/// IRQs
    struct xact int_xact;
    uint8_t* intbm;
};

typedef struct usb_hub* usb_hub_t;
int usb_hub_driver_bind(usb_dev_t usb_dev, usb_hub_t* hub);

struct usb_hubem;
typedef struct usb_hubem* usb_hubem_t;
int usb_hubem_driver_init(void* token, int nports, int pwr_delay_ms,
                          int (*set_pf)(void *token, int port, enum port_feature feature),
                          int (*clr_pf)(void *token, int port, enum port_feature feature),
                          int (*get_pstat)(void* token, int port, struct port_status* ps),
                          usb_hubem_t* hub);

int hubem_process_xact(usb_hubem_t hub, struct xact* xact, int nxact,
                       usb_cb_t completion_callback, void* token);


#endif /* _USB_DRIVERS_USBHUB_H_ */
