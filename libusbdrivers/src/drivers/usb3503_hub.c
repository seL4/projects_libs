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

#include <usb/drivers/usb3503_hub.h>
#include "../services.h"

#define REG_VIDL         0x00
#define REG_VIDM         0x01
#define REG_PIDL         0x02
#define REG_PIDM         0x03
#define REG_DIDL         0x04
#define REG_DIDM         0x05
#define REG_CFG1         0x06
#define REG_CFG2         0x07
#define REG_CFG3         0x08
#define REG_NRD          0x09
#define REG_PDS          0x0A
#define REG_PDB          0x0B
#define REG_MAXPS        0x0C
#define REG_MAXPB        0x0D
#define REG_HCMCS        0x0E
#define REG_HCMCB        0x0F
#define REG_PWRT         0x10
#define REG_LANG_ID_H    0x11
#define REG_LANG_ID_L    0x12
#define REG_MFR_STR_LEN  0x13
#define REG_PRD_STR_LEN  0x14
#define REG_SER_STR_LEN  0x15
#define REG_MFR_STR      0x16
#define REG_PROD_STR     0x54
#define REG_SER_STR      0x92
#define REG_BC_EN        0xD0
#define REG_PRTPWR       0xE5
#define REG_OCS          0xE6
#define REG_SP_ILOCK     0xE7
#define REG_INT_STATUS   0xE8
#define REG_INT_MASK     0xE9
#define REG_CFGP         0xEE
#define REG_VSNSUP3      0xF4
#define REG_VSNS21       0xF5
#define REG_BSTUP3       0xF6
#define REG_BST21        0xF8
#define REG_PRTSP        0xFA
#define REG_PRTR12       0xFB
#define REG_PRTR34       0xFC
#define REG_STCD         0xFF


static int
read_reg(usb3503_t* hub, int addr)
{
    int count;
    char data;
    count = i2c_kvslave_read(&hub->i2c_slave, addr, &data, 1);
    if (count != 1) {
        return -1;
    } else {
        return data;
    }
}

static int
write_reg(usb3503_t* hub, int addr, int data)
{
    int count;
    char cdata;
    cdata = data;
    count = i2c_kvslave_write(&hub->i2c_slave, addr, &cdata, 1);
    if (count != 1) {
        return -1;
    } else {
        return 0;
    }
}

int
usb3503_init(i2c_bus_t* i2c_bus, gpio_sys_t* gpio_sys, gpio_id_t o_nreset,
             gpio_id_t o_hubconnect, gpio_id_t i_nint, usb3503_t* hub)
{
    if (i2c_kvslave_init(i2c_bus, USB3503_I2C_ADDR, BIG8, BIG8, &hub->i2c_slave)) {
        return -1;
    }
    if (gpio_new(gpio_sys, o_nreset    , GPIO_DIR_OUT, &hub->o_nreset)) {
        return -1;
    }
    if (gpio_new(gpio_sys, o_hubconnect, GPIO_DIR_OUT, &hub->o_hubconnect)) {
        return -1;
    }
    if (gpio_new(gpio_sys, i_nint      , GPIO_DIR_OUT,  &hub->i_nint)) {
        return -1;
    }
    /* Turn off the HUB */
    gpio_clr(&hub->o_nreset);
    gpio_clr(&hub->o_hubconnect);

    /* Select Primary or Secondary reference clock
     * Primary reference clock freq (NINT high during init)
     * SEL -> FREQ
     * 00  -> 38.4 MHz
     * 01  -> 26.0 MHz
     * 10  -> 19.2 MHz
     * 11  -> 12.0 MHz
     *
     * Secondary reference clock freq (NINT low during init)
     * SEL -> FREQ
     * 00  -> 24.0 MHz
     * 01  -> 27.0 MHz
     * 10  -> 25.0 MHz
     * 11  -> 50.0 MHz
     *
     * Known platforms:
     * Odroid - SEL = 00
     */
    gpio_clr(&hub->i_nint);
    /* TODO set clock frequency */

    /* Start the init process */
    gpio_set(&hub->o_nreset);
    return 0;
}

void
usb3503_reset(usb3503_t* hub)
{
    write_reg(hub, REG_STCD, BIT(1));
}

void
usb3503_hard_reset(usb3503_t* hub)
{
    gpio_clr(&hub->o_nreset);
    udelay(100);
    gpio_set(&hub->o_nreset);
}

void
usb3503_connect(usb3503_t* hub)
{
    gpio_set(&hub->o_hubconnect);
}

void
usb3503_disconnect(usb3503_t* hub)
{
    gpio_clr(&hub->o_hubconnect);
}

void
usb3503_handle_irq(usb3503_t* hub)
{
    uint8_t status, mask;
    status = read_reg(hub, REG_INT_STATUS);
    mask = read_reg(hub, REG_INT_MASK);
    printf("HUB IRQ status: 0x%02x/0x%02x\n", status, mask);
}


