/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#ifndef USB3503_HUB_H
#define USB3503_HUB_H

/**
 * USB3503 IC from Microchip
 */
#include <platsupport/gpio.h>
#include <platsupport/i2c.h>

#define USB3503_I2C_ADDR  0x10

struct usb3503 {
    i2c_slave_t i2c_slave;
    i2c_kvslave_t kvslave;
    gpio_t o_nreset;
    gpio_t o_hubconnect;
    gpio_t i_nint;
};
typedef struct usb3503 usb3503_t;


int usb3503_init(i2c_bus_t* i2c_bus, gpio_sys_t* gpio_sys, gpio_id_t o_nreset,
                 gpio_id_t o_hubconnect, gpio_id_t i_nint, usb3503_t* hub);

void usb3503_reset(usb3503_t* hub);
void usb3503_hard_reset(usb3503_t* hub);

void usb3503_connect(usb3503_t* hub);
void usb3503_disconnect(usb3503_t* hub);

void usb3503_handle_irq(usb3503_t* hub);

#endif /* USB3503_HUB_H */
