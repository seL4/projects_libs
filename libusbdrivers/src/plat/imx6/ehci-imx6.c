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
#include <usb/usb_host.h>
#include "../usb_otg.h"
#include "../../ehci/ehci.h"
#include "../../services.h"
#include <stdio.h>
#include <stddef.h>
#include <assert.h>

#define USB_PHY1_PADDR     0x20C9000
#define USB_PHY2_PADDR     0x20CA000
#define USB_PADDR          0x2184000

#define USB_HOST1_IRQ       72
#define USB_HOST2_IRQ       73
#define USB_HOST3_IRQ       74
#define USB_OTG_IRQ         75

#define USB_PHY1_SIZE      0x0001000
#define USB_PHY2_SIZE      0x0001000
#define USB_SIZE           0x0001000

#define IMX6_PORTSC_PTS_MASK  ((3UL << 30) | (1UL << 25))
#define IMX6_PORTSC_PTS_UTMI  ((0UL << 30) | (0UL << 25))
#define IMX6_PORTSC_PTS_ULPI  ((2UL << 30) | (0UL << 25))
#define IMX6_PORTSC_PTS_ICUSB ((3UL << 30) | (0UL << 25))
#define IMX6_PORTSC_PTS_HSIC  ((4UL << 30) | (1UL << 25))
#define IMX6_PORTSC_STS       (1UL << 29)
#define IMX6_PORTSC_PTW       (1UL << 28)

struct usb_sct {
    uint32_t val;
    uint32_t set;
    uint32_t clr;
    uint32_t tog;
};


//USB_PHY1_PADDR
//USB_PHY2_PADDR
struct usb_phy_regs {
    struct usb_sct pwd;     /* 0x00 */
    struct usb_sct tx;      /* 0x10 */
    struct usb_sct rx;      /* 0x20 */
#define PHYCTRL_SFTRST               BIT(31)
#define PHYCTRL_CLKGATE              BIT(30)
#define PHYCTRL_UTMI_SUSPENDM        BIT(29)
#define PHYCTRL_HOST_FORCE_LS_SE0    BIT(28)
#define PHYCTRL_OTG_ID_VALUE         BIT(27)
#define PHYCTRL_FSDLL_RST_EN         BIT(24)
#define PHYCTRL_ENVBUSCHG_WKUP       BIT(23)
#define PHYCTRL_ENIDCHG_WKUP         BIT(22)
#define PHYCTRL_ENDPDMCHG_WKUP       BIT(21)
#define PHYCTRL_ENAUTOCLR_PHY_PWD    BIT(20)
#define PHYCTRL_ENAUTOCLR_CLKGATE    BIT(19)
#define PHYCTRL_WAKEUP_IRQ           BIT(17)
#define PHYCTRL_ENIRQWAKEUP          BIT(16)
#define PHYCTRL_ENUTMIL3             BIT(15)
#define PHYCTRL_ENUTMIL2             BIT(14)
#define PHYCTRL_DATA_ON_LRADC        BIT(13)
#define PHYCTRL_DEVPLUGIN_IRQ        BIT(12)
#define PHYCTRL_ENIRQDEVPLUGIN       BIT(11)
#define PHYCTRL_RESUME_IRQ           BIT(10)
#define PHYCTRL_ENIRQRESUMEDETECT    BIT( 9)
#define PHYCTRL_RESUMEIRQSTICKY      BIT( 8)
#define PHYCTRL_ENOTGIDDETECT        BIT( 7)
#define PHYCTRL_OTG_ID_CHG_IRQ       BIT( 6)
#define PHYCTRL_DEVPLUGIN_POLARITY   BIT( 5)
#define PHYCTRL_ENDEVPLUGINDETECT    BIT( 4)
#define PHYCTRL_HOSTDISCONDETECT_IRQ BIT( 3)
#define PHYCTRL_ENIRQHOSTDISCON      BIT( 2)
#define PHYCTRL_ENHOSTDISCONDETECT   BIT( 1)
#define PHYCTRL_ENOTG_ID_CHG_IRQ     BIT( 0)
    struct usb_sct ctrl;    /* 0x30 */
    uint32_t status;        /* 0x40 */
    uint32_t res0[3];
    struct usb_sct debug;   /* 0x50 */
    uint32_t debug0_status; /* 0x60 */
    uint32_t res1[3];
    struct usb_sct debug1;  /* 0x70 */
    uint32_t version;       /* 0x80 */
};




struct usb_otg_regs {
    uint32_t id;                    /* +0x000 */
    uint32_t hwgeneral;             /* +0x004 */
    uint32_t hwhost;                /* +0x008 */
    uint32_t otg_hwdevice;          /* +0x00C */
    uint32_t hwtxbuf;               /* +0x010 */
    uint32_t hwrxbuf;               /* +0x014 */
    uint32_t res02[26];
    uint32_t gptimer0ld;            /* +0x080 */
    uint32_t gptimer0ctrl;          /* +0x084 */
    uint32_t gptimer1ld;            /* +0x088 */
    uint32_t gptimer1ctrl;          /* +0x08C */
    uint32_t sbuscfg;               /* +0x090 */
    uint32_t res03[27];

    /* capability registers */
    uint8_t  caplength;             /* +0x100 */
    uint8_t  res05[1];
    uint16_t hciversion;            /* +0x102 */
    uint32_t hcsparams;             /* +0x104 */
    uint32_t hccparams;             /* +0x108 */
    uint32_t res06[5];
    uint32_t otg_dciversion;        /* +0x120 */
    uint32_t otg_dccparams;         /* +0x124 */
    uint32_t res08[6];

    /* operational registers */
    uint32_t usbcmd;                /* +0x140 */
    uint32_t usbsts;                /* +0x144 */
    uint32_t usbintr;               /* +0x148 */
    uint32_t frindex;               /* +0x14C */
    uint32_t res09[1];
    uint32_t otg_deviceaddr;        /* +0x154 */
    uint32_t otg_endptlistaddr;     /* +0x158 */
    uint32_t res10[1];
    uint32_t burstsize;             /* +0x160 */
    uint32_t txfilltuning;          /* +0x164 */
    uint32_t res11[4];
    uint32_t otg_endptnak;          /* +0x178 */
    uint32_t otg_endptnaken;        /* +0x17C */
    uint32_t configflag;            /* +0x180 */
    uint32_t portsc1;               /* +0x184 */

    uint32_t res13[7];
    uint32_t otg_otgsc;             /* +0x1A4 */
#define USBMODE_HOST (0x3 << 0)
#define USBMODE_DEV  (0x2 << 0)
#define USBMODE_IDLE (0x0 << 0)
    uint32_t usbmode;               /* +0x1A8 */
    uint32_t otg_endptsetupstat;    /* +0x1AC */
    uint32_t otg_endptprime;        /* +0x1B0 */
    uint32_t otg_endptflush;        /* +0x1B4 */
    uint32_t otg_endptstat;         /* +0x1B8 */
    uint32_t otg_endptcomplete;     /* +0x1BC */
    uint32_t otg_endptctrl0;        /* +0x1C0 */
    uint32_t otg_endptctrl1;        /* +0x1C4 */
    uint32_t otg_endptctrl2;        /* +0x1C8 */
    uint32_t otg_endptctrl3;        /* +0x1CC */
    uint32_t otg_endptctrl4;        /* +0x1D0 */
    uint32_t otg_endptctrl5;        /* +0x1D4 */
    uint32_t otg_endptctrl6;        /* +0x1D8 */
    uint32_t otg_endptctrl7;        /* +0x1DC */
    uint32_t res16[8];
};

struct usb_host_regs {
    uint32_t id;                    /* +0x000 */
    uint32_t hwgeneral;             /* +0x004 */
    uint32_t hwhost;                /* +0x008 */
    uint32_t res01[1];
    uint32_t hwtxbuf;               /* +0x010 */
    uint32_t hwrxbuf;               /* +0x014 */
    uint32_t res02[26];
    uint32_t gptimer0ld;            /* +0x080 */
    uint32_t gptimer0ctrl;          /* +0x084 */
    uint32_t gptimer1ld;            /* +0x088 */
    uint32_t gptimer1ctrl;          /* +0x08C */
    uint32_t sbuscfg;               /* +0x090 */
    uint32_t res03[27];

    /* capability registers */
    uint8_t  caplength;             /* +0x100 */
    uint8_t  res05[1];
    uint16_t hciversion;            /* +0x102 */
    uint32_t hcsparams;             /* +0x104 */
    uint32_t hccparams;             /* +0x108 */
    uint32_t res06[5];
    uint32_t res07[2];
    uint32_t res08[6];

    /* operational registers */
    uint32_t usbcmd;                /* +0x140 */
    uint32_t usbsts;                /* +0x144 */
    uint32_t usbintr;               /* +0x148 */
    uint32_t frindex;               /* +0x14C */
    uint32_t res09[1];
    uint32_t host_periodiclistbase; /* +0x154 */
    uint32_t host_asynclistaddr;    /* +0x158 */
    uint32_t res10[1];
    uint32_t burstsize;             /* +0x160 */
    uint32_t txfilltuning;          /* +0x164 */
    uint32_t res11[4];
    uint32_t res12[2];
    uint32_t configflag;            /* +0x180 */
    uint32_t portsc1;               /* +0x184 */

    uint32_t res13[7];
    uint32_t res14[1];

    uint32_t usbmode;               /* +0x1A8 */
    uint32_t res15[13];
    uint32_t res16[8];
};

struct usb_regs {
    /* Core */
    struct usb_otg_regs  otg;       /* +0x000 */
    struct usb_host_regs host1;     /* +0x200 */
    struct usb_host_regs host2;     /* +0x400 */
    struct usb_host_regs host3;     /* +0x600 */
    /* Non-core */
#define USBCTRL_WIR           BIT(31)
#define USBCTRL_WKUP_VBUS_EN  BIT(17)
#define USBCTRL_WKUP_ID_EN    BIT(16)
#define USBCTRL_WKUP_SW       BIT(15)
#define USBCTRL_WKUP_SW_EN    BIT(14)
#define USBCTRL_UTMI_ON_CLOCK BIT(13)
#define USBCTRL_SUSPENDM      BIT(12)
#define USBCTRL_RESET         BIT(11)
#define USBCTRL_WIE           BIT(10)
#define USBCTRL_PWR_POL       BIT( 9)
#define USBCTRL_OVER_CUR_POL  BIT( 8)
#define USBCTRL_OVER_CUR_DIS  BIT( 7)
    uint32_t otg_ctrl;              /* +0x800 */
    uint32_t host1_ctrl;            /* +0x804 */
    uint32_t host2_ctrl;            /* +0x808 */
    uint32_t host3_ctrl;            /* +0x80C */
    uint32_t host2_hsic_ctrl;       /* +0x810 */
    uint32_t host3_hsic_ctrl;       /* +0x814 */
    uint32_t otg_phy_ctrl_0;        /* +0x818 */
    uint32_t host1_phy_ctrl_0;      /* +0x81C */
};


static volatile struct usb_phy_regs *_usb_phy1_regs = NULL;
static volatile struct usb_phy_regs *_usb_phy2_regs = NULL;
static volatile struct usb_regs     *_usb_regs      = NULL;

static const int _usb_irqs[] = {
    [USB_OTG0]  = USB_OTG_IRQ,
    [USB_HOST1] = USB_HOST1_IRQ,
    [USB_HOST2] = USB_HOST2_IRQ,
    [USB_HOST3] = USB_HOST3_IRQ
};

static void
phy_enable(int devid, ps_io_ops_t* o)
{
    volatile struct usb_phy_regs* phy_regs;
    clk_t* clk;

    clock_sys_init(o, &o->clock_sys);
    switch (devid) {
    case 0:
        clk = clk_get_clock(&o->clock_sys, CLK_USB1);
        if (_usb_phy1_regs == NULL) {
            _usb_phy1_regs = GET_RESOURCE(o, USB_PHY1);
        }
        phy_regs = _usb_phy1_regs;
        assert(clk);
        assert(phy_regs);
        break;
    case 1:
        clk = clk_get_clock(&o->clock_sys, CLK_USB2);
        if (_usb_phy2_regs == NULL) {
            _usb_phy2_regs = GET_RESOURCE(o, USB_PHY2);
        }
        phy_regs = _usb_phy2_regs;
        assert(clk);
        assert(phy_regs);
        break;
    default:
        phy_regs = NULL;
        clk = NULL;
        break;
    }


    if (phy_regs) {
        if (clk == NULL) {
            LOG_ERROR("Failed to initialise USB PHY clock\n");
        }
        /* Enable clocks */
        phy_regs->ctrl.clr = PHYCTRL_CLKGATE;
        /* Reset PHY */
        phy_regs->ctrl.set = PHYCTRL_SFTRST;
        dsb();
        udelay(10);
        phy_regs->ctrl.clr = PHYCTRL_SFTRST;
        dsb();
        udelay(10);
        /* Enable PHY and FS/LS */
        phy_regs->pwd.val = 0;
        phy_regs->ctrl.set = PHYCTRL_ENUTMIL3 | PHYCTRL_ENUTMIL2;
        dsb();
        msdelay(10);
    }
}

static int
imx6_usb_generic_init(int id, ps_io_ops_t* ioops)
{
    struct usb_host_regs * hc_regs = NULL;
    volatile uint32_t* hc_ctrl;
    assert(id >= 0);
    assert(id < USB_NHOSTS);
    /* Check device mappings */
    if (_usb_regs == NULL) {
        _usb_regs = GET_RESOURCE(ioops, USB);
    }
    if (_usb_regs == NULL) {
        assert(0);
        return -1;
    }
    hc_regs = (struct usb_host_regs*)_usb_regs + id;
    hc_ctrl = &_usb_regs->otg_ctrl + id;

    /* Reset the EHCI controller */
    hc_regs->usbcmd |= EHCICMD_HCRESET;
    /* Disable over-current */
    *hc_ctrl |= USBCTRL_OVER_CUR_POL;
    *hc_ctrl |= USBCTRL_OVER_CUR_DIS;
    /* Enable the PHY */
    phy_enable(id, ioops);
    return 0;
}


int
usb_host_init(enum usb_host_id id, ps_io_ops_t* ioops, usb_host_t* hdev)
{
    struct usb_host_regs * hc_regs = NULL;
    int err;
    if (id < 0 || id > USB_NHOSTS) {
        return -1;
    }
    assert(ioops);
    assert(hdev);

    hdev->id = id;
    hdev->dman = &ioops->dma_manager;

    err = imx6_usb_generic_init(hdev->id, ioops);
    if (err) {
        assert(0);
        return -1;
    }
    /* Pass control to EHCI initialisation */
    hc_regs = (struct usb_host_regs*)_usb_regs + hdev->id;
    hc_regs->usbmode = USBMODE_HOST;
    err = ehci_host_init(hdev, (uintptr_t)&hc_regs->caplength, NULL);
    /* Configure ports */
    hc_regs->portsc1 = IMX6_PORTSC_PTS_UTMI | IMX6_PORTSC_PTW;
    return err;
}

const int*
usb_host_irqs(usb_host_t* host, int* nirqs)
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

int
usb_plat_otg_init(usb_otg_t odev, ps_io_ops_t* ioops)
{
    struct usb_otg_regs* otg_regs;
    int err;
    assert(odev->dman);
    assert(odev->id == 0);
    err = imx6_usb_generic_init(odev->id, ioops);
    if (err) {
        assert(0);
        return -1;
    }
    otg_regs = (struct usb_otg_regs*)_usb_regs + odev->id;
    otg_regs->usbmode = USBMODE_DEV;
    err = ehci_otg_init(odev, (uintptr_t)&otg_regs->caplength);
    assert(otg_regs->usbmode == USBMODE_DEV);
    return err;
}


