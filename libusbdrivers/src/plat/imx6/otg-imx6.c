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
#include <string.h>
#include <stdio.h>

#define ZF_LOG_LEVEL ZF_LOG_DEBUG

#include <stdlib.h>
#include <string.h>
#include "../../services.h"
#include "../usb_otg.h"

/*
 * The implementation here comes from the imx6 reference manual
 * and is considered proprietary.
 */

#define MAX_PKT_SIZE 64

struct ehci_host_cap {
    uint8_t  caplength;        /* +0x00 */
    uint8_t  res0[4];
    uint32_t hcsp_portroute;   /* +0x0C */
    uint32_t res1[4];
    /* OTG only */
    uint16_t dciversion;         /* +0x20 */
    uint32_t dccparams;          /* +0x24 */
};

struct ehci_otg_op {
#define EHCICMD_IRQTHRES(x)   (((x) & 0xff) * BIT(16))
#define EHCICMD_IRQTHRES_MASK EHCICMD_IRQTHRES(0xff)
#define OTGCMD_DTDTRIP        BIT(14)
#define OTGCMD_SETUPTRIP      BIT(13)
#define EHCICMD_LIGHT_RST     BIT(7)
#define EHCICMD_HCRESET       BIT(1)
#define EHCICMD_RUNSTOP       BIT(0)
    uint32_t usbcmd;             /* +0x00 */
#define EHCISTS_HCHALTED      BIT(12)
#define OTGSTS_SLEEP          BIT( 8)
#define OTGSTS_RESET          BIT( 6)
#define EHCISTS_PORTC_DET     BIT( 2)
#define EHCISTS_USBERRINT     BIT( 1)
#define EHCISTS_USBINT        BIT( 0)
    uint32_t usbsts;             /* +0x04 */
#define OTGINTR_SLEEP         BIT( 8)
#define OTGINTR_RESET         BIT( 6)
#define EHCIINTR_PORTC_DET    BIT( 2)
#define EHCIINTR_USBERRINT    BIT( 1)
#define EHCIINTR_USBINT       BIT( 0)
    uint32_t usbintr;            /* +0x08 */
    uint32_t frindex;            /* +0x0C */
    uint32_t ctrldssegment;      /* +0x10 */
#define OTG_DEVADDR(x)        (((x) & 0x7f) << 25)
#define OTG_DEVADDR_ADV       BIT(24)
    uint32_t otg_deviceaddr;     /* +0x14 */
    /* Align to 2K */
    uint32_t otg_endptlistaddr;  /* +0x18 */
    uint32_t res0[1];
    uint32_t burstsize;          /* +0x20 */
    uint32_t res1[5];
    uint32_t otg_endptnak;       /* +0x38 */
    uint32_t otg_endptnaken;     /* +0x3C */
    uint32_t res2[1];
#define EHCI_PORT_WO_OCURRENT  BIT(22)
#define EHCI_PORT_WO_DCONNECT  BIT(21)
#define EHCI_PORT_WO_CONNECT   BIT(20)
#define EHCI_PORT_OWNER        BIT(13)
#define EHCI_PORT_POWER        BIT(12)
#define EHCI_PORT_JSTATE       BIT(11)
#define EHCI_PORT_KSTATE       BIT(10)
#define EHCI_PORT_SPEED_MASK   (EHCI_PORT_JSTATE | EHCI_PORT_KSTATE)
#define EHCI_PORT_RESET        BIT( 8)
#define EHCI_PORT_SUSPEND      BIT( 7)
#define EHCI_PORT_FORCE_RESUME BIT( 6)
#define EHCI_PORT_OCURRENT_C   BIT( 5)
#define EHCI_PORT_OCURRENT     BIT( 4)
#define EHCI_PORT_ENABLE_C     BIT( 3)
#define EHCI_PORT_ENABLE       BIT( 2)
#define EHCI_PORT_CONNECT_C    BIT( 1)
#define EHCI_PORT_CONNECT      BIT( 0)
#define EHCI_PORT_CHANGE       (EHCI_PORT_OCURRENT_C | \
                                EHCI_PORT_ENABLE_C   | \
                                EHCI_PORT_CONNECT_C)
    uint32_t portsc1;            /* +0x44 */
    uint32_t res3[7];
    /* IRQ enable */
#define OTGSC_DATA_PULSE_IRQEN      BIT(30)
#define OTGSC_1MS_IRQEN             BIT(29)
#define OTGSC_BSESSION_END_IRQEN    BIT(28)
#define OTGSC_BSESSION_VALID_IRQEN  BIT(27)
#define OTGSC_ASESSION_VALID_IRQEN  BIT(26)
#define OTGSC_AVBUS_VALID_IRQEN     BIT(25)
#define OTGSC_USBID_IRQEN           BIT(24)
    /* IRQ status */
#define OTGSC_DATA_PULSE_IRQSTS     BIT(22)
#define OTGSC_1MS_IRQSTS            BIT(21)
#define OTGSC_BSESSION_END_IRQSTS   BIT(20)
#define OTGSC_BSESSION_VALID_IRQSTS BIT(19)
#define OTGSC_ASESSION_VALID_IRQSTS BIT(18)
#define OTGSC_AVBUS_VALID_IRQSTS    BIT(17)
#define OTGSC_USBID_IRQSTS          BIT(16)
    /* Signal status */
#define OTGSC_DATA_PULSE_STS        BIT(14)
#define OTGSC_1MS_STS               BIT(13)
#define OTGSC_BSESSION_END_STS      BIT(12)
#define OTGSC_BSESSION_VALID_STS    BIT(11)
#define OTGSC_ASESSION_VALID_STS    BIT(10)
#define OTGSC_AVBUS_VALID_STS       BIT( 9)
#define OTGSC_USBID_STS             BIT( 8)
    /* Line control */
#define OTGSC_IDPULLUP              BIT( 5)
#define OTGSC_DATAPULSE             BIT( 4)
#define OTGSC_OTGTERMINATION        BIT( 3)
#define OTGSC_VBUSCHARGE            BIT( 1)
#define OTGSC_vBUSDISCHARGE         BIT( 0)
    uint32_t otg_otgsc;          /* +0x64 */
#define USBMODE_HOST (0x3 << 0)
#define USBMODE_DEV  (0x2 << 0)
#define USBMODE_IDLE (0x0 << 0)
    uint32_t usbmode;            /* +0x68 */
#define OTGRX(ep)  ((ep) <<  0)
#define OTGTX(ep)  ((ep) << 16)
    uint32_t otg_endptsetupstat; /* +0x6C */
    uint32_t otg_endptprime;     /* +0x70 */
    uint32_t otg_endptflush;     /* +0x74 */
    uint32_t otg_endptstat;      /* +0x78 */
    uint32_t otg_endptcomplete;  /* +0x7C */
#define OTGEPCTRL_TXE    OTGTX(BIT(7))
#define OTGEPCTRL_TXT(x) OTGTX(((x) & 0x3) << 2)
#define OTGEPCTRL_TXS    OTGTX(BIT(0))
#define OTGEPCTRL_RXE    OTGRX(BIT(7))
#define OTGEPCTRL_RXT(x) OTGRX(((x) & 0x3) << 2)
#define OTGEPCTRL_RXS    OTGRX(BIT(0))
    uint32_t otg_endptctrl[8];   /* +0x80 */
};

struct dTD {
#define DTDNEXT_INVALID      0x1
    uint32_t dTD_next;
#define DTDTOK_BYTES(x)    (((x) & 0x7fff) << 16)
#define DTDTOK_IOC         BIT(15)
#define DTDTOK_MULTO(x)    (((x) & 0x3) << 10)
#define DTDTOK_ACTIVE      BIT(7)
#define DTDTOK_HALTED      BIT(6)
#define DTDTOK_BUFERR      BIT(5)
#define DTDTOK_XACTERR     BIT(4)
    uint32_t token;
#define DTDBUF0_OFFSET(x)  (((x) & 0xfff) << 0)
#define DTDBUF1_FRAME(x)   (((x) & 0x7ff) << 0)
    uint32_t buf[5];
    uint32_t res;
};

/* 48 bytes aligned to 64 byte */
/* 12 words; */
struct dQH {
#define EPQHCAP_MULT(x)    (((x) & 0x3) << 30)
#define EPQHCAP_ZLT_DIS    BIT(29)
#define EPQHCAP_MAX_PKT(x) (((x) & 0x7ff) << 16)
#define EPQHCAP_IOS        BIT(15)
    uint32_t ep_cap;
    uint32_t dTD_cur;
    struct dTD overlay;
    uint32_t usbreq[2];
    /* For alignment
     * Freescale suggests maintaining head/tail ptrs here */
    uint32_t align[4];
};

struct dTDn {
    volatile struct dTD* dtd;
    uintptr_t pdtd;
    void *buf;
    uintptr_t pbuf;
    otg_prime_cb cb;
    void* token;
    struct dTDn* next;
};

struct otg_ep {
    volatile struct dQH* dqh;
    struct dTDn* dtdn;
    int ep;
};

struct ehci_otg {
    int devid;
    volatile struct ehci_host_cap * cap_regs;
    volatile struct ehci_otg_op * op_regs;
    /* Number of endpoints provided by this driver */
    int nep;
    /* DMA memory for endpoint QH array */
    void* dqh;
    uintptr_t pdqh;
    /* Array of endpoint QH and TD tuples */
    struct otg_ep* ep;
    /* Setup packet callback data
     * TODO should this be plat dependant? */
    otg_setup_cb setup_cb;
    void* setup_token;
};

struct usb_otg_data {
    struct ehci_otg otg;
};

/***************
 **** DEBUG ****
 ***************/
static void dump_odev(struct ehci_otg*)        UNUSED;
static void dump_dtd(volatile struct dTD*)     UNUSED;
static void dump_buf(void* buf, int len)       UNUSED;
static void dump_dtdn(struct dTDn* dtdn)       UNUSED;
static void dump_dqh(volatile struct dQH* dqh) UNUSED;
static void dump_eplist(struct ehci_otg* odev) UNUSED;
static void dump_ep(struct otg_ep* ep)         UNUSED;

static void
dump_odev(struct ehci_otg* odev)
{
    uint32_t sts, cmd, intr;
    volatile uint32_t *epctrl;
    int i, j;
    sts = odev->op_regs->usbsts;
    cmd = odev->op_regs->usbcmd;
    intr = odev->op_regs->usbintr;
    epctrl = odev->op_regs->otg_endptctrl;
    printf("*** OTG odevice ***\n");
    printf("* usbcmd\n");
    printf("  IRQ threshold: 0x%x\n", (cmd >> 16) & 0xff);
    printf("     light reset: %s\n", (cmd & EHCICMD_LIGHT_RST) ? "yes" : "no");
    printf("         hcreset: %s\n", (cmd & EHCICMD_HCRESET) ? "yes" : "no");
    printf("       hcrunstop: %s\n", (cmd & EHCICMD_RUNSTOP) ? "yes" : "no");
    printf("* usbsts\n");
    printf("       hc halted: %s\n", (sts & EHCISTS_HCHALTED) ? "yes" : "no");
    printf("     port change: %s\n", (sts & EHCISTS_PORTC_DET) ? "yes" : "no");
    printf("         usb err: %s\n", (sts & EHCISTS_USBERRINT) ? "yes" : "no");
    printf("       usb event: %s\n", (sts & EHCISTS_USBINT) ? "yes" : "no");
    printf("* usbintr\n");
    printf("     port change: %s\n", (intr & EHCIINTR_PORTC_DET) ? "yes" : "no");
    printf("         usb err: %s\n", (intr & EHCIINTR_USBERRINT) ? "yes" : "no");
    printf("       usb event: %s\n", (intr & EHCIINTR_USBINT) ? "yes" : "no");
    printf(" *  Address: 0x%x\n", odev->op_regs->otg_deviceaddr >> 25);
    printf(" *  ep base: 0x%x\n", odev->op_regs->otg_endptlistaddr);
    printf(" * ep setup: 0x%x\n", odev->op_regs->otg_endptsetupstat);
    printf(" * ep prime: 0x%x\n", odev->op_regs->otg_endptprime);
    printf(" *  ep stat: 0x%x\n", odev->op_regs->otg_endptstat);
    printf(" *  ep done: 0x%x\n", odev->op_regs->otg_endptcomplete);
    printf(" * ep flush: 0x%x\n", odev->op_regs->otg_endptflush);
    for (i = 0; i < odev->nep; i++) {
        uint32_t v = epctrl[i];
        printf("  ep%d: 0x%08x | ", i, v);
        for (j = 0; j < 32; j += 16) {
            v >>= j;
            printf("%s:", (j) ? "RX" : "TX");
            printf("%s,", (v & OTGEPCTRL_RXE) ? "run " : "stop");
            printf("t:0x%x,", (v >> 2) & 0x3);
            printf("%s,", (v & OTGEPCTRL_RXS) ? "Stall" : " OK  ");
        }
        printf("\n");
    }
}
void
dump_regs(volatile struct ehci_otg_op* op_regs)
{
    uint32_t sts, cmd, intr, portsc1, mode,
     addr, otsc, epss;
    cmd = op_regs->usbcmd;
    sts = op_regs->usbsts;
    intr = op_regs->usbintr;
    addr = op_regs->otg_deviceaddr;
    portsc1 = op_regs->portsc1;
    mode = op_regs->usbmode;
    otsc = op_regs->otg_otgsc;
    epss = op_regs->otg_endptsetupstat;
    printf("*** regs ***\n");
    printf("  cmd: 0x%x\n", cmd);
    printf("  sts: 0x%x\n", sts);
    printf("  int: 0x%x\n", intr);
    printf(" addr: 0x%x\n", addr);
    printf("  sc1: 0x%x\n", portsc1);
    printf(" mode: 0x%x\n", mode);
    printf(" otsc: 0x%x\n", otsc);
    printf(" epss: 0x%x\n", epss);
}

static void
dump_dtd(volatile struct dTD* dtd)
{
    const char* col = A_FG_Y;
    int i;
    printf("%s", col);
    printf("DTD @ 0x%x\n", (uint32_t)dtd);
    printf("%s", col);
    printf("   next: 0x%08x | %s\n", dtd->dTD_next,
           (dtd->dTD_next & DTDNEXT_INVALID) ?
           "invalid" : "valid");
    printf("  token: 0x%08x | ", dtd->token);
    printf("%d bytes, ", (dtd->token >> 16) & 0x7fff);
    if (dtd->token & DTDTOK_IOC) {
        printf("IOC,");
    }
    printf("multo=%d,", (dtd->token >> 10) & 0x3);
    if (dtd->token & DTDTOK_ACTIVE) {
        printf("ACTIVE ");
    }
    if (dtd->token & DTDTOK_HALTED) {
        printf("HALTED ");
    }
    if (dtd->token & DTDTOK_BUFERR) {
        printf("BUFERR ");
    }
    if (dtd->token & DTDTOK_XACTERR) {
        printf("XACTERR ");
    }
    printf("\n");
    for (i = 0; i < 5; i++) {
        printf("   buf%d: 0x%x\n", i, dtd->buf[i]);
    }
    printf(A_FG_RESET);
}

static void
dump_buf(void* buf, int len)
{
    uint8_t* d = buf;
    while (len--) {
        printf("%02x", *d++);
    }
    printf("\n");
}

static void
dump_dtdn(struct dTDn* dtdn)
{
    volatile struct dTD* dtd = dtdn->dtd;
    if (dtd) {
        dump_dtd(dtd);
        if (dtdn->next) {
            dump_dtdn(dtdn->next);
            dump_buf(dtdn->buf, 0x12);
        }
    }
}

static void
dump_dqh(volatile struct dQH* dqh)
{
    const char* col = A_FG_M;
    struct usbreq* r = (struct usbreq*)dqh->usbreq;
    printf("%s", col);
    printf("DQH @ 0x%x\n", (uint32_t)dqh);
    printf("%s", col);
    printf(" ep cap: 0x%x | ", dqh->ep_cap);
    printf("mult: %d,", (dqh->ep_cap >> 30) & 0x3);
    printf("zlt %sabled,", (dqh->ep_cap & EPQHCAP_ZLT_DIS) ?
           "dis" : "en");
    printf("max packet=%d,", (dqh->ep_cap >> 16) & 0x7ff);
    if (dqh->ep_cap & EPQHCAP_IOS) {
        printf("ios");
    }
    printf("\n");
    printf("current: 0x%08x\n", dqh->dTD_cur);
    printf("  setup: t0x%02x|r0x%02x|v0x%04x|i0x%04x|s0x%04x\n",
           r->bmRequestType, r->bRequest, r->wValue,
           r->wIndex, r->wLength);
    dump_dtd(&dqh->overlay);
}

static void
dump_ep(struct otg_ep* ep)
{
    dump_dqh(ep->dqh);
    if (ep->dtdn) {
        dump_dtdn(ep->dtdn);
    }
}

static void
dump_eplist(struct ehci_otg* odev)
{
    int i;
    for (i = 0; i < odev->nep; i++) {
        if (odev->op_regs->otg_endptctrl[i] & (OTGEPCTRL_TXE | OTGEPCTRL_RXE)) {
            printf("Endpoint %d:\n", i);
            dump_ep(&odev->ep[2 * i]);
            dump_ep(&odev->ep[2 * i + 1]);
        }
    }
}

/****************************
 **** Descriptor helpers ****
 ****************************/

static enum usb_xact_status
dtd_get_status(volatile struct dTD* dtd)
{
    uint32_t t = dtd->token;
    if (t & (DTDTOK_BUFERR | DTDTOK_XACTERR)) {
        return XACTSTAT_ERROR;
    } else if (t & DTDTOK_HALTED) {
        return XACTSTAT_HOSTERROR;
    } else if (t & DTDTOK_ACTIVE) {
        return XACTSTAT_PENDING;
    } else {
        return XACTSTAT_SUCCESS;
    }
}

static struct dTDn*
otg_dtdn_new(usb_otg_t otg, void* buf, uintptr_t pbuf, int len) {
    ZF_LOGD("otg_dtdn_new");
    struct dTDn* dtdn;
    volatile struct dTD* dtd;
    int cur_len;
    int i;

    if (otg == NULL) {
	    ZF_LOGF("Invalid arguments\n");
    }

    /* Allocate a descriptor */
    dtdn = usb_malloc(sizeof(*dtdn));
    if (dtdn == NULL) {
	ZF_LOGE("OTG: Out of memory\n");
        return NULL;
    }
    dtd = ps_dma_alloc_pinned(otg->dman, sizeof(*dtdn->dtd), 32,
                              0, PS_MEM_NORMAL, &dtdn->pdtd);
    if (dtd == NULL) {
        ZF_LOGE("OTG: Out of DMA memory\n");
        return NULL;
    }
    dtdn->dtd = dtd;
    dtdn->buf = buf;
    dtdn->cb = NULL;
    dtdn->token = NULL;
    dtdn->next = NULL;
    /* Initialise the DTD */
    dtd->dTD_next = DTDNEXT_INVALID;
    dtd->token = DTDTOK_BYTES(len) | DTDTOK_IOC
                 | DTDTOK_MULTO(0) | DTDTOK_ACTIVE;
    cur_len = 0;
    for (i = 0; i < sizeof(dtd->buf) / sizeof(*dtd->buf); i++) {
        if (cur_len < len) {
            int this_len;
            dtd->buf[i] = pbuf;
            this_len = 0x1000 - (pbuf & 0xfff);
            cur_len += this_len;
            pbuf += this_len;
        } else {
            dtd->buf[i] = 0;
        }
    }
    return dtdn;
}


/***********************
 **** EP operations ****
 ***********************/
static void
flush_ep(usb_otg_t otg, struct otg_ep* ep)
{
    ZF_LOGD("flush_ep");
    struct ehci_otg* odev;
    odev = &otg->pdata->otg;
    odev->op_regs->otg_endptflush = OTGTX(BIT(ep->ep));
    while (ep->dtdn) {
        ep->dtdn = ep->dtdn->next;
    }
    ep->dqh->overlay.dTD_next = DTDNEXT_INVALID;
}

static int
imx6_otg_ep0_setup(usb_otg_t otg, otg_setup_cb cb, void* token)
{
    ZF_LOGD("imx6_otg_ep0_setup");
    struct ehci_otg* odev = &otg->pdata->otg;
    odev->setup_cb = cb;
    odev->setup_token = token;
    odev->op_regs->usbcmd |= EHCICMD_RUNSTOP;
    return 0;
}

static int
imx6_otg_prime(usb_otg_t otg, int epno, enum usb_xact_type dir,
               void* buf, uintptr_t pbuf, int len,
               otg_prime_cb cb, void* token)
{
    ZF_LOGD("imx6_otg_prime");
    struct otg_ep* ep;
    struct ehci_otg* odev;
    struct dTDn** dtdn_ptr;
    struct dTDn* dtdn;
    volatile struct dTD* dtd_prev;
    uint32_t epbit;

    if (!otg || epno < 0) {
	    ZF_LOGF("OTG: Invalid arguments\n");
    }

    odev = &otg->pdata->otg;
    ep = &odev->ep[2 * epno];
    if (dir == PID_IN) {
        ep++;
    }
    /* Create the descriptor */
    dtdn = otg_dtdn_new(otg, buf, pbuf, len);
    if (dtdn == NULL) {
        ZF_LOGE("OTG: Failed to create descriptor\n");;
        return -1;
    }
    dtdn->cb = cb;
    dtdn->token = token;
    /* Add to the tail of the dTD node list */
    dtdn_ptr = &ep->dtdn;
    dtd_prev = &ep->dqh->overlay;
    while (*dtdn_ptr) {
        dtd_prev = (*dtdn_ptr)->dtd;
        dtdn_ptr = &(*dtdn_ptr)->next;
    }
    *dtdn_ptr = dtdn;
    /* Ensure the driver will process it */
    if (dir == PID_IN) {
        epbit = OTGTX(BIT(ep->ep));
    } else {
        epbit = OTGRX(BIT(ep->ep));
    }
    /* imx6 64.4.6.5.3 */
    /* Check DCD driver to see if pipe is empty */
    if (dtd_prev->dTD_next != DTDNEXT_INVALID) {
	    ZF_LOGF("OTG: Empty pipe\n");
    }

    if (ep->dqh->overlay.dTD_next != DTDNEXT_INVALID) {
        /* Case 2: List is not empty */
        /* 1) Add dTD to end of linked list */
        dtd_prev->dTD_next = dtdn->pdtd;
        /* 2) Read prime bit */
        if (odev->op_regs->otg_endptprime & epbit) {
            /* DONE */
            return 0;
        } else {
            uint32_t sts;
            do {
                /* 3) Set trip */
                odev->op_regs->usbcmd |= OTGCMD_DTDTRIP;
                /* 4) Read endpoint status */
                sts = odev->op_regs->otg_endptstat;
                /* 5) Read trip, if 0 goto 3 */
            } while (!(odev->op_regs->usbcmd &= OTGCMD_DTDTRIP));
            /* 6) Write 0 to clear trip */
            odev->op_regs->usbcmd &= ~OTGCMD_DTDTRIP;
            if (sts & epbit) {
                /* DONE */
                return 0;
            } else {
                /* Goto case 1 */
            }
        }
    }
    /* Case 1: List is empty */
    /* 1) Write dQH next pointer */
    dtd_prev->dTD_next = dtdn->pdtd;
    /* 2) Clear active and halt bit in dQH */
    ep->dqh->overlay.token &= ~(DTDTOK_HALTED | DTDTOK_ACTIVE);
    /* 3) Prime endpoint */
    odev->op_regs->otg_endptprime |= epbit;
    /* DONE */
    return 0;
}

/************************
 **** IRQ operations ****
 ************************/
static void
otg_handle_reset(usb_otg_t otg)
{
    ZF_LOGD("otg_handle_reset");
    struct ehci_otg* odev;
    uint32_t v;
    int i;
    odev = &otg->pdata->otg;
    /* imx6 Chapter 64.4.6.2.1 */
    /* Clear all setup token semaphores */
    v = odev->op_regs->otg_endptstat;
    odev->op_regs->otg_endptstat = v;
    /* Clear all enpoint complete status bits */
    v = odev->op_regs->otg_endptcomplete;
    odev->op_regs->otg_endptcomplete = v;
    /* Cancel all primed status */
    while (odev->op_regs->otg_endptprime);
    odev->op_regs->otg_endptflush = 0xFFFFFFFF;
    /* Clear descriptors */
    for (i = 0; i < odev->nep * 2; i++) {
        flush_ep(otg, &odev->ep[i]);
    }
}


static void
otg_handle_setup(usb_otg_t otg, struct otg_ep* ep)
{
    ZF_LOGD("otg_handle_setup");
    struct ehci_otg* odev;
    struct usbreq req;

    if (!otg || !otg->pdata) {
	    ZF_LOGF("Invalid arguments\n");
    }

    odev = &otg->pdata->otg;
    flush_ep(otg, ep + 1);
    /* imx6 64.4.6.3.2.1 */
    /* a) write 1 to clear setup stat */
    odev->op_regs->otg_endptsetupstat = OTGRX(BIT(ep->ep));
    do {
        /* b) set tripwire */
        odev->op_regs->usbcmd |= OTGCMD_SETUPTRIP;
        /* c) duplicate setup */
        memcpy(&req, (void*)ep->dqh->usbreq, sizeof(req));
        /* d) read trip wire and goto 2 if not set */
    } while (!(odev->op_regs->usbcmd & OTGCMD_SETUPTRIP));
    /* e) write 0 to clear trip */
    odev->op_regs->usbcmd &= ~OTGCMD_SETUPTRIP;
    /* f) Process setup packet */
    odev = &otg->pdata->otg;
    if (req.bRequest == SET_ADDRESS) {
        int addr = req.wValue;
        ZF_LOGD("OTG: New address %d\n", addr);
        odev->op_regs->otg_deviceaddr = OTG_DEVADDR(addr)
                                        | OTG_DEVADDR_ADV;
        otg_prime(otg, ep->ep, PID_IN, NULL, 0, 0, NULL, NULL);
    } else {
        odev->setup_cb(otg, odev->setup_token, &req);
    }
}

static void
otg_handle_complete(usb_otg_t otg, struct otg_ep* ep)
{
    ZF_LOGD("otg_handle_complete");
    struct dTDn* dtdn;
    dtdn = ep->dtdn;

    if (!dtdn) {
	    ZF_LOGF("Invalid arguments\n");
    }

    while (dtdn) {
        enum usb_xact_status stat;
        struct dTDn* tmp;
        stat = dtd_get_status(dtdn->dtd);
        if (stat == XACTSTAT_PENDING) {
            break;
        }
        if (dtdn->cb) {
            dtdn->cb(otg, dtdn->token, stat);
        }
        ps_dma_free_pinned(otg->dman, (void*)dtdn->dtd, sizeof(*dtdn->dtd));
        tmp = dtdn;
        dtdn = dtdn->next;
        usb_free(tmp);
    }
    ep->dtdn = dtdn;
}


static void
otg_handle_int(usb_otg_t otg)
{
    ZF_LOGD("otg_handle_int");
    struct ehci_otg* odev;
    uint32_t v;
    int e;
    odev = &otg->pdata->otg;
    /* Check for completion */
    v = odev->op_regs->otg_endptcomplete;
    for (e = 0; e < 32; e++) {
        if (v & BIT(e)) {
            struct otg_ep* ep = odev->ep + ((e & 0xf) * 2);
            odev->op_regs->otg_endptcomplete = BIT(e);
            if (e < 16) {
                ZF_LOGD("OTG: EP %d RX complete\n", ep->ep);
            } else {
                ep++;
                ZF_LOGD("OTG: EP %d TX complete\n", ep->ep);
            }
            otg_handle_complete(otg, ep);
        }
    }
    /* Check for setup packet received */
    v = odev->op_regs->otg_endptsetupstat;
    for (e = 0; e < 32; e++) {
        if (v & BIT(e)) {
            ZF_LOGD("OTG: EP %d Received setup", e);
            otg_handle_setup(otg, &odev->ep[e * 2]);
        }
    }
}

/****************************
 **** Exported functions ****
 ****************************/
void
otg_plat_handle_irq(usb_otg_t otg)
{
    struct ehci_otg* odev;
    uint32_t sts;
    odev = &otg->pdata->otg;
    sts = odev->op_regs->usbsts;
    sts &= odev->op_regs->usbintr; // mask the status with the enabled interrupts
    if (sts & EHCISTS_USBINT) {
        ZF_LOGD("OTG: INT - USB int");
        odev->op_regs->usbsts = EHCISTS_USBINT;
        sts &= ~EHCISTS_USBINT;
        otg_handle_int(otg);
    }
    if (sts & EHCISTS_USBERRINT) {
        ZF_LOGD("OTG: INT - USB error");
        odev->op_regs->usbsts = EHCISTS_USBERRINT;
        sts &= ~EHCISTS_USBERRINT;
    }
    if (sts & EHCISTS_PORTC_DET) {
        ZF_LOGD("OTG: Port change: connect");
        odev->op_regs->usbsts = EHCISTS_PORTC_DET;
        sts &= ~EHCISTS_PORTC_DET;
    }
    if (sts & OTGSTS_SLEEP) {
        ZF_LOGD("OTG: Port change: sleep");
        odev->op_regs->usbsts = OTGSTS_SLEEP;
        sts &= ~OTGSTS_SLEEP;
    }
    if (sts & OTGSTS_RESET) {
        ZF_LOGD("OTG: Reset request");
        odev->op_regs->usbsts = OTGSTS_RESET;
        sts &= ~OTGSTS_RESET;
        otg_handle_reset(otg);
    }
    if (sts) {
        ZF_LOGF("Unhandled USB irq. Status: 0x%x", sts);
    }
}

int
ehci_otg_init(usb_otg_t odev, uintptr_t cap_regs)
{
    ZF_LOGD("ehci_otg_init");
    struct ehci_otg * otg;
    struct dQH* dqh_list;
    struct otg_ep* ep;
    int i;
    odev->pdata = usb_malloc(sizeof(*odev->pdata));
    if (!odev->pdata) {
	    ZF_LOGE("Out of memory\n");
	    return -1;
    }
    odev->ep0_setup = &imx6_otg_ep0_setup;
    odev->prime = &imx6_otg_prime;

    otg = &odev->pdata->otg;
    otg->devid = odev->id;
    otg->cap_regs = (void*)cap_regs;
    otg->op_regs = (void*)(cap_regs + otg->cap_regs->caplength);
    /* Setup endpoints */
    otg->nep = (otg->cap_regs->dccparams & 0x1f);
    dqh_list = ps_dma_alloc_pinned(odev->dman,
                                   sizeof(*otg->ep[0].dqh) * otg->nep * 2,
                                   2048, 0, PS_MEM_NORMAL, &otg->pdqh);
    if (dqh_list == NULL) {
        ZF_LOGE("Out of DMA memory\n");
        return -1;
    }
    otg->ep = usb_malloc(sizeof(*otg->ep) * otg->nep * 2);
    if (otg->ep == NULL) {
        ZF_LOGE("Out of memory\n");
        return -1;
    }
    for (i = 0, ep = otg->ep; i < otg->nep * 2; i++, ep++) {
        ep->dqh = &dqh_list[i];
        memset((void*)ep->dqh, 0, sizeof(*ep->dqh));
        /* imx6 64.4.6.4.1 */
        ep->dqh->ep_cap = EPQHCAP_MAX_PKT(128)
                          | EPQHCAP_MULT(0)
                          | EPQHCAP_IOS;
        ep->dqh->overlay.dTD_next = DTDNEXT_INVALID;

        ep->ep = i >> 1;
    }
    /* Initialise the controller */
    otg->op_regs->otg_deviceaddr = 0;
    otg->op_regs->otg_endptlistaddr = otg->pdqh;
    /* flush eps */
    otg->op_regs->otg_endptnaken = 0x10001;
    otg->op_regs->otg_endptcomplete |= 0;
    otg->op_regs->otg_endptsetupstat |= 0;
    otg->op_regs->otg_endptflush = 0xffffffff;
    // wait for host to stop signalling reset?
    while(otg->op_regs->portsc1 & 0x100);
    //! - Clear reset status bit
    otg->op_regs->usbsts |=  (0x80 | 0x40 | 0x01);

    /* Enable EP0 */
    otg->op_regs->otg_endptctrl[0] = OTGEPCTRL_TXE | OTGEPCTRL_RXE;
    /* start the controller */
    otg->op_regs->usbintr = EHCIINTR_USBINT
                            | EHCIINTR_USBERRINT
                            | EHCIINTR_PORTC_DET
                            | OTGINTR_RESET
                            | OTGINTR_SLEEP;

    otg->op_regs->otg_otgsc |= OTGSC_DATA_PULSE_IRQEN
                               |  OTGSC_1MS_IRQEN
                               |  OTGSC_BSESSION_END_IRQEN
                               |  OTGSC_BSESSION_VALID_IRQEN
                               |  OTGSC_ASESSION_VALID_IRQEN
                               |  OTGSC_AVBUS_VALID_IRQEN
                               |  OTGSC_USBID_IRQEN
                               |  OTGSC_IDPULLUP
                               |  OTGSC_OTGTERMINATION;


    return 0;
}

