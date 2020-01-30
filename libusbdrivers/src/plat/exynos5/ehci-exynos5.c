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
#include <platsupport/clock.h>
#include <platsupport/gpio.h>
#include <platsupport/plat/gpio.h>
#include <platsupport/mach/pmic.h>
#include <platsupport/plat/sysreg.h>
#include <usb/usb_host.h>
#include "../../ehci/ehci.h"
#include "../../services.h"
#include "../usb_otg.h"
#include <stdio.h>
#include <stddef.h>
#include <usb/drivers/usb3503_hub.h>


#define USB2_HOST_CTRL_PADDR     0x12130000
#define USB2_HOST_CTRL_SIZE      0x1000
#define USB2_HOST_EHCI_PADDR     0x12110000
#define USB2_HOST_EHCI_SIZE      0x1000
#define USB2_HOST_OHCI_PADDR     0x12120000
#define USB2_HOST_OHCI_SIZE      0x1000
#define USB2_DEV_LINK_PADDR      0x12140000
#define USB2_DEV_LINK_SIZE       0x1000

#define USB2_HOST_IRQ            103

#define USBPHY_CTRL0_OFFSET      0x000
#define USBPHY_CTRL1_OFFSET      0x010
#define USBPHY_CTRL2_OFFSET      0x020
#define USBPHY_EHCI_OFFSET       0x030

#define USBPHY_CTRL_RESETALL     BIT(31)
#define USBPHY_CTRL0_SRCSEL(x)   (((x) & 0x03) << 19)
#define USBPHY_CTRLX_SRCSEL(x)   (((x) & 0x03) << 23)
#define USBPHY_CTRL0_FREQ_SEL(x) (((x) & 0x07) << 16)
#define USBPHY_CTRLX_FREQ_SEL(x) (((x) & 0x7F) << 16)
#define USBPHY_CTRL_UTMI_RESET   BIT(2)
#define USBPHY_CTRL_LINK_RESET   BIT(1)
#define USBPHY_CTRL_PHY_RESET    BIT(0)
#define USBPHY_CTRL_RESET        ( USBPHY_CTRL_UTMI_RESET | \
                                   USBPHY_CTRL_LINK_RESET | \
                                   USBPHY_CTRL_PHY_RESET  | \
                                   USBPHY_CTRL_RESETALL   )

#define USBPHY_EHCI_ENABLE       (BIT(29) | BIT(28) | BIT(27) | BIT(26))

#define REG32(base, offset)      (volatile uint32_t*)((void*)(base) + (offset))
#define PHYREG32(base, o)        REG32(base, USBPHY_##o##_OFFSET)

#define NRESET_GPIO              XEINT12
#define HUBCONNECT_GPIO          XEINT6
#define NINT_GPIO                XEINT7

static volatile void *_phy_regs = NULL;

static sysreg_t _sysreg;

/* EHCI registers */
static void *_usb_regs = NULL;
static const int _usb_irqs[] = {
    [USB_HOST0] = USB2_HOST_IRQ
};

/* GPIO subsystem for bit-bangined I2C and HUB control */
static gpio_sys_t gpio_sys;

/* HUB and PMIC on I2C4 */
static struct i2c_bb i2c_bb;
static i2c_bus_t i2c_bus;

/* Eth power control */
static pmic_t pmic;
/* Hub control */
static usb3503_t usb3503_hub;

static int usb_init_phy(ps_io_ops_t *io_ops)
{
    /* Map phy regs */
    if (_phy_regs == NULL) {
        _phy_regs = GET_RESOURCE(io_ops, USB2_HOST_CTRL);
    }
    exynos5_sysreg_init(io_ops, &_sysreg);
    exynos5_sysreg_usbphy_enable(USBPHY_USB2, &_sysreg);

    /* Hold in reset */
    *PHYREG32(_phy_regs, CTRL0) |= USBPHY_CTRL_RESET;
    *PHYREG32(_phy_regs, CTRL1) |= USBPHY_CTRL_RESET;
    *PHYREG32(_phy_regs, CTRL2) |= USBPHY_CTRL_RESET;
    ps_udelay(10);

    /* Setup clocks and enable PHY */
    *PHYREG32(_phy_regs, CTRL0) = USBPHY_CTRL0_FREQ_SEL(0x05) | USBPHY_CTRL0_SRCSEL(2) | BIT(10);
    *PHYREG32(_phy_regs, CTRL1) = USBPHY_CTRLX_FREQ_SEL(0x24) | USBPHY_CTRLX_SRCSEL(2);
    *PHYREG32(_phy_regs, CTRL2) = USBPHY_CTRLX_FREQ_SEL(0x24) | USBPHY_CTRLX_SRCSEL(2);

    /* Enable the EHCI controller */
    *PHYREG32(_phy_regs, EHCI) |= USBPHY_EHCI_ENABLE;

    ps_udelay(40);

    return 0;
}



/*******************************************/

static void hub_pwren(int state)
{
    if (state) {
        usb3503_connect(&usb3503_hub);
    } else {
        usb3503_disconnect(&usb3503_hub);
    }
}

static void eth_pwren(int state)
{
    if (state) {
        pmic_ldo_cfg(&pmic, LDO_ETH, LDO_ON, 3300);
        ps_mdelay(40);
    } else {
        pmic_ldo_cfg(&pmic, LDO_ETH, LDO_OFF, 3300);
    }
}

static void board_pwren(int port, int state)
{
    switch (port) {
    case 1:
        /* USB2 */
        break;
    case 2:
        /* HSIC Ethernet */
        eth_pwren(state);
        break;
    case 3:
        /* USB hub */
        hub_pwren(state);
        break;
    default:
        ZF_LOGF("Invalid port for power change\n");
    }
}

static int usb_plat_gpio_init(ps_io_ops_t *io_ops)
{
    int err;
    err = gpio_sys_init(io_ops, &gpio_sys);
    if (err) {
        ZF_LOGF("GPIO error\n");
    }
    err = i2c_bb_init(&gpio_sys, GPIOID(GPA2, 1), GPIOID(GPA2, 0), &i2c_bb, &i2c_bus);
    if (err) {
        ZF_LOGF("I2C error\n");
    }

    /* USB hub */
    err = usb3503_init(&i2c_bus, &gpio_sys, NRESET_GPIO, HUBCONNECT_GPIO,
                       NINT_GPIO, &usb3503_hub);
    if (err) {
        ZF_LOGF("Hub3503 error\n");
    }

    /* PMIC for ethernet power change */
    err = pmic_init(&i2c_bus, PMIC_BUSADDR, &pmic);
    if (err) {
        ZF_LOGF("PMIC error\n");
    }

    /* Turn off the eth chip */
    eth_pwren(0);
    return err;
}

int usb_host_init(enum usb_host_id id, ps_io_ops_t *io_ops, ps_mutex_ops_t *sync,
                  usb_host_t *hdev)
{
    int err;
    if (id < 0 || id > USB_NHOSTS) {
        return -1;
    }

    if (!io_ops || !hdev) {
        ZF_LOGF("Invalid arguments\n");
    }

    hdev->id = id;
    hdev->dman = &io_ops->dma_manager;
    hdev->sync = sync;

    /* Check device mappings */
    if (_usb_regs == NULL) {
        _usb_regs = GET_RESOURCE(io_ops, USB2_HOST_EHCI);
    }
    if (_usb_regs == NULL) {
        return -1;
    }

    /* Initialise GPIOs */
    if (usb_plat_gpio_init(io_ops)) {
        return -1;
    }

    /* Initialise the phy */
    if (usb_init_phy(io_ops)) {
        ZF_LOGE("PHY transceiver error");
        return -1;
    }

    err = ehci_host_init(hdev, (uintptr_t)_usb_regs, &board_pwren);

    return err;
}

const int *usb_host_irqs(usb_host_t *host, int *nirqs)
{
    if (host->id < 0 || host->id > USB_NHOSTS) {
        return NULL;
    }

    if (nirqs) {
        *nirqs = 1;
    }

    host->irqs = &_usb_irqs[host->id];
    return host->irqs;
}

const int *usb_otg_irqs(usb_otg_t otg, int *nirqs)
{
    return NULL;
}

int usb_plat_otg_init(usb_otg_t odev, ps_io_ops_t *io_ops)
{
    return -1;
}


