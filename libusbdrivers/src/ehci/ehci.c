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

#include "ehci.h"

#include <string.h>
#include <stdio.h>
#include <assert.h>

#include <stdlib.h>
#include <string.h>
#include <usb/drivers/usbhub.h>
#include "../services.h"
#include <assert.h>

//#define EHCI_DEBUG_IRQ
#define EHCI_DEBUG
//#define EHCI_TRAFFIC_DEBUG

#ifdef EHCI_DEBUG
#define dprintf(...) printf(__VA_ARGS__)
#else
#define dprintf(...) do{}while(0)
#endif

#ifdef EHCI_DEBUG_IRQ
#define EHCI_IRQDBG(...) EHCI_DBG(__VA_ARGS__)
#else
#define EHCI_IRQDBG(...) do{}while(0)
#endif

#define EHCI_DBG(host, ...)                     \
        do {                                    \
            struct ehci_host* h = host;         \
            if(h){                              \
                dprintf("EHCI %1d: ", h->devid);\
            }else{                              \
                dprintf("EHCI  ?: ");           \
            }                                   \
            dprintf(__VA_ARGS__);               \
        }while(0)


/*******************
 **** Registers ****
 *******************/

struct ehci_host_cap {
    uint8_t  caplength;        /* +0x00 */
    uint8_t  res0[1];
    uint16_t hciversion;       /* +0x02 */
#define EHCI_HCS_N_PORTS(x)    (((x) & 0xf) >> 0)
    uint32_t hcsparams;        /* +0x04 */
#define EHCI_HCC_PFRAMELIST    BIT(1)
#define EHCI_HCC_64BIT         BIT(0)
    uint32_t hccparams;        /* +0x08 */
    uint32_t hcsp_portroute;   /* +0x0C */
    uint32_t res1[4];
    /* OTG only */
    uint16_t dciversion;         /* +0x20 */
    uint32_t dccparams;          /* +0x24 */
};

struct ehci_host_op {
#define EHCICMD_IRQTHRES(x)   (((x) & 0xff) * BIT(16))
#define EHCICMD_IRQTHRES_MASK EHCICMD_IRQTHRES(0xff)
#define EHCICMD_ASYNC_PARK    BIT(11)
#define EHCICMD_ASYNC_PARKM   (((x) &  0x3) * BIT( 8))
#define EHCICMD_LIGHT_RST     BIT(7)
#define EHCICMD_ASYNC_DB      BIT(6)
#define EHCICMD_ASYNC_EN      BIT(5)
#define EHCICMD_PERI_EN       BIT(4)
#define EHCICMD_LIST_S1024    (0x0 * BIT(2))
#define EHCICMD_LIST_S512     (0x1 * BIT(2))
#define EHCICMD_LIST_S256     (0x2 * BIT(2))
#define EHCICMD_LIST_SMASK    (0x3 * BIT(2))
#define EHCICMD_HCRESET       BIT(1)
#define EHCICMD_RUNSTOP       BIT(0)
    uint32_t usbcmd;           /* +0x00 */
#define EHCISTS_ASYNC_EN      BIT(15)
#define EHCISTS_PERI_EN       BIT(14)
#define EHCISTS_ASYNC_EMPTY   BIT(13)
#define EHCISTS_HCHALTED      BIT(12)
#define EHCISTS_ASYNC_ADV     BIT( 5)
#define EHCISTS_HOST_ERR      BIT( 4)
#define EHCISTS_FLIST_ROLL    BIT( 3)
#define EHCISTS_PORTC_DET     BIT( 2)
#define EHCISTS_USBERRINT     BIT( 1)
#define EHCISTS_USBINT        BIT( 0)
    uint32_t usbsts;           /* +0x04 */
#define EHCIINTR_ASYNC_ADV    BIT( 5)
#define EHCIINTR_HOST_ERR     BIT( 4)
#define EHCIINTR_FLIST_ROLL   BIT( 3)
#define EHCIINTR_PORTC_DET    BIT( 2)
#define EHCIINTR_USBERRINT    BIT( 1)
#define EHCIINTR_USBINT       BIT( 0)
    uint32_t usbintr;          /* +0x08 */
/// Translate a frame index into a micro frame index
#define FRAME2UFRAME(x)       ((x) << 3)
/// Translate a micro frame index into a frame index
#define UFRAME2FRAME(x)       ((x) >> 3)
#define FRINDEX_UF(x)         ((x) & 0x7)
    uint32_t frindex;          /* +0x0C */
    uint32_t ctrldssegment;    /* +0x10 */
    uint32_t periodiclistbase; /* +0x14 */
    uint32_t asynclistaddr;    /* +0x18 */
    uint32_t res0[9];
#define EHCICFLAG_CFLAG        BIT( 0)
    uint32_t configflag;       /* +0x40 */
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

    uint32_t portsc[];         /* +0x44 */
};


/*********************
 **** Descriptors ****
 *********************/

struct TD {
#define TDLP_INVALID           BIT(0)
    uint32_t next;
#define TDALTTDPTR_NAKCNT(x)   (((x) >> 1) & 0x7)
    uint32_t alt;
#define TDTOK_DT               BIT(31)
#define TDTOK_BYTES(x)         (((x) & 0x7fff) << 16)
#define TDTOK_BYTES_MASK       TDTOK_BYTES(0x7fff)
#define TDTOK_GET_BYTES(x)     (((x) & TDTOK_BYTES_MASK) >> 16)
#define TDTOK_IOC              BIT(15)
#define TDTOK_C_PAGE(x)        (((x) & 0x7) * BIT(12))
#define TDTOK_C_PAGE_MASK      TDTOK_C_PAGE(0x7)
#define TDTOK_C_ERR(x)         (((x) & 0x3) * BIT(10))
#define TDTOK_C_ERR_MASK       TDTOK_C_ERR(0x3)
#define TDTOK_PID_OUT          (0 * BIT(8))
#define TDTOK_PID_IN           (1 * BIT(8))
#define TDTOK_PID_SETUP        (2 * BIT(8))
#define TDTOK_SACTIVE          BIT(7)
#define TDTOK_SHALTED          BIT(6)
#define TDTOK_SBUFERR          BIT(5)
#define TDTOK_SBABDET          BIT(4)
#define TDTOK_SXACTERR         BIT(3)
#define TDTOK_SUFRAME_MISS     BIT(2)
#define TDTOK_SSPLITXSTATE     BIT(1)
#define TDTOK_PINGSTATE        BIT(0)
#define TDTOK_ERROR            (TDTOK_SHALTED  | \
                                TDTOK_SBUFERR  | \
                                TDTOK_SBABDET  | \
                                TDTOK_SXACTERR | \
                                TDTOK_SUFRAME_MISS)
    uint32_t token;
#define TDBUF0_CUROFFSET(x)    (((x) & 0xfff) * BIT(0))
#define TDBUF0_CUROFFSET_MASK  TDBUF0_CUROFFSET(0xfff)
#define QHBUF1_CPROGMASK(x)    (((x) &  0xff) * BIT(0))
#define QHBUF2_SBYTES(x)       (((x) &  0xff) * BIT(5))
#define QHBUF2_FRAMETAG(x)     (((x) &   0xf) * BIT(0))
    uint32_t buf[5];
};

struct QH {
#define QHLP_TYPE_ITD          (0x0 * BIT(1))
#define QHLP_TYPE_QH           (0x1 * BIT(1))
#define QHLP_TYPE_SITD         (0x2 * BIT(1))
#define QHLP_TYPE_FSTN         (0x3 * BIT(1))
#define QHLP_INVALID           BIT(0)
    uint32_t qhlptr;
#define QHEPC0_NAKCNT_RL(x)    (((x) &  0xf) * BIT(28))
#define QHEPC0_NAKCNT_RL_MASK  QHEPC0_NAKCNT_RL(0xf)
#define QHEPC0_C               BIT(27)
#define QHEPC0_MAXPKTLEN(x)    (((x) & 0x7ff) * BIT(16))
#define QHEPC0_H               BIT(15)
#define QHEPC0_DTC             BIT(14)
#define QHEPC0_FSPEED          (0 * BIT(12))
#define QHEPC0_LSPEED          (1 * BIT(12))
#define QHEPC0_HSPEED          (2 * BIT(12))
#define QHEPC0_EP(x)           (((x) &  0xf) * BIT( 8))
#define QHEPC0_I               BIT(7)
#define QHEPC0_ADDR(x)         (((x) & 0x7f) * BIT( 0))
#define QHEPC1_MULT(x)         (((x) &  0x3) * BIT(30))
#define QHEPC1_PORT(x)         (((x) & 0x7f) * BIT(23))
#define QHEPC1_HUB_ADDR(x)     (((x) & 0x7f) * BIT(16))
#define QHEPC1_UFRAME_CMASK(x) (((x) & 0xff) * BIT( 8))
#define QHEPC1_UFRAME_SMASK(x) (((x) & 0xff) * BIT( 0))
#define QHEPC1_UFRAME_MASK     (QHEPC1_UFRAME_CMASK(0xff) | \
                                QHEPC1_UFRAME_CMASK(0xff))
    uint32_t epc[2];
    uint32_t td_cur;
    struct TD td_overlay;
};


/****************************
 **** Private structures ****
 ****************************/

struct TDn {
    volatile struct TD* td;
    uintptr_t ptd;
    struct xact xact;
};

struct QHn {
    /* Transaction data */
    volatile struct QH* qh;
    uintptr_t pqh;
    int ntdns;
    struct TDn* tdns;
    /* Interrupts */
    int rate;
    usb_cb_t cb;
    void* token;
    int irq_pending;
    int was_cancelled;
    /* Links */
    uint8_t owner_addr;
    struct QHn* next;
};

struct ehci_host {
    int devid;
    /* Hub emulation */
    usb_hubem_t hubem;
    void (*board_pwren)(int port, int state);
    /* IRQ data */
    struct xact irq_xact;
    usb_cb_t irq_cb;
    void* irq_token;
    uint32_t bmreset_c;
    /* Async schedule */
    struct QHn* alist_tail;
    struct QHn* db_pending;
    struct QHn* db_active;
    /* Periodic frame list */
    uint32_t* flist;
    uintptr_t pflist;
    int flist_size;
    struct QHn* intn_list;
    /* Standard registers */
    volatile struct ehci_host_cap * cap_regs;
    volatile struct ehci_host_op  * op_regs;
    /* Support */
    ps_dma_man_t* dman;
    void* state;
};

struct usb_hc_data {
    struct ehci_host edev;
};

/*****************
 **** Helpers ****
 *****************/

static inline struct ehci_host*
_hcd_to_ehci(usb_host_t* hcd) {
    struct usb_hc_data* hc_data = (struct usb_hc_data*)hcd->pdata;
    assert(hc_data);
    return &hc_data->edev;
}

static inline int
_is_enabled_periodic(struct ehci_host* edev)
{
    return edev->op_regs->usbsts & EHCISTS_PERI_EN;
}

static inline void
_enable_periodic(struct ehci_host* edev)
{
    edev->op_regs->usbcmd |= EHCICMD_PERI_EN;
    while (!_is_enabled_periodic(edev));
}

static inline void
_disable_periodic(struct ehci_host* edev)
{
    edev->op_regs->usbcmd &= ~EHCICMD_PERI_EN;
    while (_is_enabled_periodic(edev));
}

static inline int
_is_enabled_async(struct ehci_host* edev)
{
    return edev->op_regs->usbsts & EHCISTS_ASYNC_EN;
}

static inline void
_enable_async(struct ehci_host* edev)
{
    edev->op_regs->usbcmd |= EHCICMD_ASYNC_EN;
    while (!_is_enabled_async(edev));
}

static inline void
_disable_async(struct ehci_host* edev)
{
    edev->op_regs->usbcmd &= ~EHCICMD_ASYNC_EN;
    while (_is_enabled_async(edev));
}

static inline int
_is_ehci_running(struct ehci_host* edev)
{
    return edev->op_regs->usbsts & EHCISTS_HCHALTED;
}

static inline void
_ehci_run(struct ehci_host* edev)
{
    edev->op_regs->usbcmd |= EHCICMD_RUNSTOP;
    while (!_is_ehci_running(edev));
}

static inline void
_ehci_stop(struct ehci_host* edev)
{
    edev->op_regs->usbcmd |= EHCICMD_RUNSTOP;
    while (_is_ehci_running(edev));
}

static enum usb_xact_status
qtd_get_status(volatile struct TD* qtd)
{
    uint32_t t = qtd->token;
    if (t & TDTOK_SACTIVE) {
        /* Note that we have already returned an error code
         * if this TD is still pending due to an error in
         * a previous TD */
        return XACTSTAT_PENDING;

    } else if (t & TDTOK_SHALTED){
        if (t & TDTOK_SXACTERR) {
            return XACTSTAT_ERROR;
        } else if (t & TDTOK_ERROR) {
            return XACTSTAT_HOSTERROR;
        }
        printf("EHCI: Unknown QTD error code 0x%x\n", t);
        return XACTSTAT_HOSTERROR;

    } else {
        return XACTSTAT_SUCCESS;
    }
}

static enum usb_xact_status
qhn_get_status(struct QHn * qhn)
{
    int i;
    if (qhn->ntdns) {
        /* If the QHN has not been picked up by the HC yet, the
         * overlay will not be valid. Check the status of the TDs */
        for (i = 0; i < qhn->ntdns; i++) {
            enum usb_xact_status stat;
            stat = qtd_get_status(qhn->tdns[i].td);
            if (stat != XACTSTAT_SUCCESS) {
                return stat;
            }
        }
    }
    /* All TDs complete, return the status of the QH */
    return qtd_get_status(&qhn->qh->td_overlay);
}

static inline int
_qhn_is_active(struct QHn* qhn)
{
    return qhn->qh->td_overlay.token & TDTOK_SACTIVE;
}

static inline int
qhn_get_bytes_remaining(struct QHn *qhn)
{
    int sum = 0;
    int i;

    for (i = 0; i < qhn->ntdns; i++) {
       sum += TDTOK_GET_BYTES(qhn->tdns[i].td->token);
    }

    return sum;
}

static inline int
qhn_cb(struct QHn *qhn, enum usb_xact_status stat)
{
    return qhn->cb(qhn->token, stat, qhn_get_bytes_remaining(qhn));
}

/****** DEBUG printing *******/
static const char*
dump_colour(enum usb_xact_status stat)
{
    switch (stat) {
    case XACTSTAT_PENDING:
        return CYELLOW;
    case XACTSTAT_ERROR:
    case XACTSTAT_HOSTERROR:
        return CRED;
    case XACTSTAT_SUCCESS:
        return CGREEN;
    default:
        return "";
    }
}

static void
dump_qtd(volatile struct TD* qtd)
{
    int pid;
    uint32_t tok;
    const char* col = dump_colour(qtd_get_status(qtd));
    printf(CINVERT"%s", col);
    printf("-- td 0x%08x\n", (uint32_t)qtd);
    printf(CREGULAR"%s", col);
    printf("-    next: 0x%08x | 0x%08x (%s)\n",
           qtd->next, qtd->next & ~0x1f,
           (qtd->next & 0x1) ? "TERMINATE" : "CONTINUE");
    printf("- altnext: 0x%08x | 0x%08x (%s)\n",
           qtd->alt, qtd->alt & ~0x1f,
           (qtd->alt & 0x1) ? "TERMINATE" : "CONTINUE");
    /* Complicated token */
    tok = qtd->token;
    printf("-   token: 0x%08x | ", tok);
    printf("%s", (tok & (1UL << 31)) ? "TOG;" : "");
    printf("totx %d;", (tok >> 16) & 0x7fff);
    printf("%s", (tok & (1UL << 15)) ? "IOC;" : "");
    printf("page %d;", (tok >> 12) & 0x7);
    printf("errs %d;", (tok >> 10) & 0x3);
    pid = (tok >> 8) & 0x3;
    printf("pid %d (%s)", pid, (pid == 0) ? "OUT" :
           (pid == 1) ? "IN" :
           (pid == 2) ? "SETUP" : "RES");
    if (tok & BIT(7)) {
        printf(";ACTIVE");
    }
    if (tok & BIT(6)) {
        printf(";HALTED");
    }
    if (tok & BIT(5)) {
        printf(";BUFFER ERROR");
    }
    if (tok & BIT(4)) {
        printf(";BABBLE DETECTED");
    }
    if (tok & BIT(3)) {
        printf(";TRANSACTION ERR");
    }
    if (tok & BIT(2)) {
        printf(";MISSED uFRAME");
    }
    if (tok & BIT(1)) {
        printf(";COMPLETE SPLIT");
    }
    if (tok & BIT(0)) {
        printf(";DO PING");
    }
    printf("\n");
    /* buffer list */
    {
        int i;
        for (i = 0; i < 5; i++) {
            if (qtd->buf[i] & ~(0x1000 - 1)) {
                printf("- buffer%d: 0x%08x | 0x%08x",
                       i, qtd->buf[i], qtd->buf[i] & ~0xfff);

                if (i == 0) {
                    printf(" (0x%x bytes left)\n", qtd->buf[i] & 0xfff);
                } else {
                    printf("\n");
                }
            }
        }
    }
    set_colour(COL_DEF);
}

static void
dump_qhn(struct QHn* qhn)
{
    uint32_t v;
    volatile struct QH* qh;
    const char* col;
    int i;
    col = dump_colour(qhn_get_status(qhn));
    qh = qhn->qh;
    printf(CINVERT"%s", col);
    printf("++ qh 0x%08x\n", (uint32_t)qh);
    printf(CREGULAR"%s", col);
    printf("+ link: 0x%08x | 0x%08x (%s|",
           qh->qhlptr, qh->qhlptr & ~0xf,
           (qh->qhlptr & 0x1) ? "TERMINATE" : "CONTINUE");
    switch ((qh->qhlptr >> 1) & 0x3) {
    case 0:
        printf("ITD)\n") ;
        break;
    case 1:
        printf("QH)\n")  ;
        break;
    case 2:
        printf("SITD)\n");
        break;
    case 3:
        printf("FSTN)\n");
        break;
    }
    v = qh->epc[0];
    printf("+ epc0: 0x%08x| addr %d(%d); NAC reload %d;max pkt %d",
           v, v & 0x7f, (v >> 8) & 0xf, v >> 28, (v >> 16) & 0x7ff);
    if (v & (1 << 27)) {
        printf(";C");
    }
    if (v & (1 << 15)) {
        printf(";H");
    }
    if (v & (1 << 14)) {
        printf(";DTC");
    }
    if (v & (1 <<  7)) {
        printf(";I");
    }

    v = (v >> 12) & 0x3;
    switch (v) {
    case 0:
        printf("; 12Mbs");
        break;
    case 1:
        printf(";1.5Mbs");
        break;
    case 2:
        printf(";480Mbs");
        break;
    case 3:
        printf(";???Mbs");
        break;
    }
    printf("\n");
    printf("C-PROG Mask: 0x%x s-bytes 0x%x frametag 0x%x\n",
           (qh->td_overlay.buf[1] >> 0) & 0xff,
           (qh->td_overlay.buf[2] >> 5) & 0x7f,
           (qh->td_overlay.buf[2] >> 0) & 0x1f);
    v = qh->epc[1];
    printf("+ epc1: 0x%08x; hub %d; port %d; txc %d; uF c%d s%d\n",
           v, (v >> 16) & 0x7f, (v >> 23) & 0x7f, v >> 30,
           (v >> 8) & 0xff, v & 0xff );
    printf("+ current: 0x%08x\n", qh->td_cur);
    dump_qtd(&qh->td_overlay);
    for (i = 0; i < qhn->ntdns; i++) {
        dump_qtd(qhn->tdns[i].td);
    }
    set_colour(COL_DEF);
}

static void UNUSED
dump_q(struct QHn* qhn)
{
    int i = 1;
    struct QHn* head = qhn;
    printf("##### Start Async list #####\n");
    do {
        printf("{QH %d}\n", i++);
        dump_qhn(qhn);
        qhn = qhn->next;
    } while (qhn && qhn != head);
    printf("##### End Async list #####\n");
}

static void UNUSED
dump_edev(struct ehci_host* edev)
{
    uint32_t sts, cmd, intr;
    sts = edev->op_regs->usbsts;
    cmd = edev->op_regs->usbcmd;
    intr = edev->op_regs->usbintr;
    printf("*** EHCI edevice ***\n");
    printf("* usbcmd\n");
    printf("   IRQ threshold: 0x%x\n", (cmd >> 16) & 0xff);
    printf("    async parked: %s\n", (cmd & EHCICMD_ASYNC_PARK) ? "yes" : "no");
    printf(" async park mode: %d\n", (cmd >> 8) & 0x3);
    printf("     light reset: %s\n", (cmd & EHCICMD_LIGHT_RST) ? "yes" : "no");
    printf("  async doorbell: %s\n", (cmd & EHCICMD_ASYNC_DB) ? "yes" : "no");
    printf("           async: %s\n", (cmd & EHCICMD_ASYNC_EN) ? "Enabled" : "Disabled");
    printf(" frame list size: ");
    switch (cmd & EHCICMD_LIST_SMASK) {
    case EHCICMD_LIST_S1024:
        printf("1024\n");
        break;
    case EHCICMD_LIST_S512 :
        printf("512\n") ;
        break;
    case EHCICMD_LIST_S256 :
        printf("256\n") ;
        break;
    default:
        printf("Unknown\n");
    }
    printf("         hcreset: %s\n", (cmd & EHCICMD_HCRESET) ? "yes" : "no");
    printf("       hcrunstop: %s\n", (cmd & EHCICMD_RUNSTOP) ? "yes" : "no");
    printf("* usbsts\n");
    printf("        async en: %s\n", (sts & EHCISTS_ASYNC_EN) ? "yes" : "no");
    printf("     periodic en: %s\n", (sts & EHCISTS_PERI_EN) ? "yes" : "no");
    printf("     async empty: %s\n", (sts & EHCISTS_ASYNC_EMPTY) ? "yes" : "no");
    printf("       hc halted: %s\n", (sts & EHCISTS_HCHALTED) ? "yes" : "no");
    printf("   async advance: %s\n", (sts & EHCISTS_ASYNC_ADV) ? "yes" : "no");
    printf("        host err: %s\n", (sts & EHCISTS_HOST_ERR) ? "yes" : "no");
    printf(" frame list roll: %s\n", (sts & EHCISTS_FLIST_ROLL) ? "yes" : "no");
    printf("     port change: %s\n", (sts & EHCISTS_PORTC_DET) ? "yes" : "no");
    printf("         usb err: %s\n", (sts & EHCISTS_USBERRINT) ? "yes" : "no");
    printf("       usb event: %s\n", (sts & EHCISTS_USBINT) ? "yes" : "no");
    printf("* usbintr\n");
    printf("   async advance: %s\n", (intr & EHCIINTR_ASYNC_ADV) ? "yes" : "no");
    printf("        host err: %s\n", (intr & EHCIINTR_HOST_ERR) ? "yes" : "no");
    printf(" frame list roll: %s\n", (intr & EHCIINTR_FLIST_ROLL) ? "yes" : "no");
    printf("     port change: %s\n", (intr & EHCIINTR_PORTC_DET) ? "yes" : "no");
    printf("         usb err: %s\n", (intr & EHCIINTR_USBERRINT) ? "yes" : "no");
    printf("       usb event: %s\n", (intr & EHCIINTR_USBINT) ? "yes" : "no");
    printf(" *   Frame index: 0x%x\n", edev->op_regs->frindex);
    printf(" * periodic base: 0x%x\n", edev->op_regs->periodiclistbase);
    printf(" *    async base: 0x%x\n", edev->op_regs->asynclistaddr);
}

/***************************
 *** Hub emulation stubs ***
 ***************************/
static inline volatile uint32_t*
_get_portsc(struct ehci_host* h, int port)
{
    volatile uint32_t *reg;
    assert(port > 0);
    assert(port <= EHCI_HCS_N_PORTS(h->cap_regs->hcsparams));
    reg = &h->op_regs->portsc[port - 1];
    return reg;
}

static int
_set_pf(void *token, int port, enum port_feature feature)
{
    struct ehci_host* edev = (struct ehci_host*)token;
    volatile uint32_t* ps_reg = _get_portsc(edev, port);
    /* Change bits are write-1-to-clear so need to mask then */
    uint32_t v = *ps_reg & ~(EHCI_PORT_CHANGE);
    switch (feature) {
    case PORT_ENABLE:
        v |= EHCI_PORT_ENABLE;
        break;
    case PORT_POWER:
        if (edev->board_pwren) {
            edev->board_pwren(port, 1);
        }
        v |= EHCI_PORT_POWER;
        break;
    case PORT_RESET:
        /* HCHALTED bit in USBSTS should be a zero */
        assert((edev->op_regs->usbsts & EHCISTS_HCHALTED) == 0);
        edev->bmreset_c = BIT(port);
        v &= ~EHCI_PORT_ENABLE;
        v |= EHCI_PORT_RESET;
        /* Perform the reset */
        *ps_reg = v;
        /* Sabre will automatically stop the reset and a ENABLE CHANGE
         * IRQ event fires, but this does not happen on the Odroid! */
        /* Wait for reset to complete */
        msdelay(10); /* 7.1.7.5 of USB 0.2 10ms delay */
        *ps_reg &= ~EHCI_PORT_RESET;
        while (*ps_reg & EHCI_PORT_RESET);

        return 0;
    case PORT_SUSPEND:
        /* Must be enabled */
        assert(*ps_reg & EHCI_PORT_ENABLE);
        /* Must portowner 0 */
        assert(!(*ps_reg & EHCI_PORT_OWNER));
        /* Perform the Suspend */
        v |= EHCI_PORT_SUSPEND;
        break;
    default:
        printf("EHCI: Unknown feature %d for set feature request\n", feature);
        return -1;
    }
    *ps_reg = v;
    return 0;
}

static int
_clr_pf(void *token, int port, enum port_feature feature)
{
    struct ehci_host* edev = (struct ehci_host*)token;
    volatile uint32_t* ps_reg = _get_portsc(edev, port);
    /* Change bits are write-1-to-clear so need to mask then */
    uint32_t v = *ps_reg & ~(EHCI_PORT_CHANGE);
    switch (feature) {
    case PORT_ENABLE        :
        v &= ~EHCI_PORT_ENABLE   ;
        break;
    case PORT_OVER_CURRENT  :
        v &= ~EHCI_PORT_OCURRENT ;
        break;
    case C_PORT_ENABLE      :
        v |= EHCI_PORT_ENABLE_C  ;
        break;
    case C_PORT_CONNECTION  :
        v |= EHCI_PORT_CONNECT_C ;
        break;
    case C_PORT_OVER_CURRENT:
        v |= EHCI_PORT_OCURRENT_C;
        break;
    case C_PORT_RESET:
        edev->bmreset_c &= ~BIT(port);
        break;
    case PORT_POWER:
        v &= ~EHCI_PORT_POWER;
        if (edev->board_pwren) {
            edev->board_pwren(port, 0);
        }
        break;
    case PORT_SUSPEND:
        /* Must be enabled */
        assert(v & EHCI_PORT_ENABLE);
        /* Must be in suspend state */
        assert(v & EHCI_PORT_SUSPEND);
        /* Perform the Suspend */
        v |= EHCI_PORT_FORCE_RESUME;
        break;

    default:
        printf("EHCI: Unknown feature %d for clear feature request\n", feature);
        return -1;
    }
    udelay(10);
    *ps_reg = v;
    udelay(10);
    return 0;
}

static int
_get_pstat(void* token, int port, struct port_status* _ps)
{
    struct ehci_host* edev = (struct ehci_host*)token;
    uint32_t v;
    struct port_status ps;
    v = *_get_portsc(edev, port);
    /* Hey EHCI, here's an idea: Why not pull your spec inline with the USB hub spec? */
    ps.wPortStatus =
        ((v & EHCI_PORT_CONNECT   ) ? BIT(PORT_CONNECTION  ) : 0) |
        ((v & EHCI_PORT_ENABLE    ) ? BIT(PORT_ENABLE      ) : 0) |
        ((v & EHCI_PORT_SUSPEND   ) ? BIT(PORT_SUSPEND     ) : 0) |
        ((v & EHCI_PORT_OCURRENT  ) ? BIT(PORT_OVER_CURRENT) : 0) |
        ((v & EHCI_PORT_RESET     ) ? BIT(PORT_RESET       ) : 0) |
        ((v & EHCI_PORT_POWER     ) ? BIT(PORT_POWER       ) : 0) |
        0;
    ps.wPortChange =
        ((v & EHCI_PORT_CONNECT_C ) ? BIT(PORT_CONNECTION  ) : 0) |
        ((v & EHCI_PORT_ENABLE_C  ) ? BIT(PORT_ENABLE      ) : 0) |
        ((v & EHCI_PORT_OCURRENT_C) ? BIT(PORT_OVER_CURRENT) : 0) |
        0;
    /* Set up the speed */
    if (v & EHCI_PORT_JSTATE) {
        /* Full speed */
    } else if (v & EHCI_PORT_KSTATE) {
        ps.wPortStatus |= BIT(PORT_LOW_SPEED);
    } else {
        ps.wPortStatus |= BIT(PORT_HIGH_SPEED);
    }
    /* Emulate reset complete */
    if (!(v & EHCI_PORT_RESET) && (edev->bmreset_c & BIT(port))) {
        ps.wPortChange |= BIT(PORT_RESET);
    }
    *_ps = ps;
    return 0;
}


/****************************
 **** Queue manipulation ****
 ****************************/

static int
td_set_buf(volatile struct TD* td, uintptr_t buf, int len)
{
    int i = 0;
    usb_assert(td);
    /* Reset the length field in the TD */
    td->token &= ~(TDTOK_BYTES_MASK | TDTOK_C_PAGE_MASK);
    /* Fill the buffers */
    if (len && buf) {
        uintptr_t buf_end = buf + len;
        /* Write the first buffer if we are not page aligned */
        if (buf & 0xfff) {
            td->buf[i++] = buf;
            buf = (buf & ~0xfff) + 0x1000;
        }
        /* Write subsequent pages */
        while (buf < buf_end) {
            if (i >= sizeof(td->buf) / sizeof(*td->buf)) {
                DBG_MEM("Out of TD buffer fields\n");
                return -1;
            } else {
                td->buf[i++] = buf;
                buf += 0x1000;
            }
        }
        td->token |= TDTOK_BYTES(len);
    }
    /* Clear remaining buffers */
    while (i < sizeof(td->buf) / sizeof(*td->buf)) {
        td->buf[i++] = 0;
    }
    /* Done! */
    return 0;
}

static struct QHn*
qhn_new(struct ehci_host* edev, uint8_t address, uint8_t hub_addr,
        uint8_t hub_port, enum usb_speed speed, int ep, int max_pkt,
        int dt, struct xact* xact, int nxact, usb_cb_t cb, void* token) {
    struct QHn *qhn;
    volatile struct QH* qh;
    volatile struct TD* prev_td;
    int i;

    assert(nxact >= 1);

    /* Allocate book keeping node */
    qhn = (struct QHn*)malloc(sizeof(*qhn));
    assert(qhn);
    qhn->ntdns = nxact;
    qhn->rate = 0;
    qhn->cb = cb;
    qhn->token = token;
    qhn->irq_pending = 0;
    qhn->was_cancelled = 0;
    qhn->owner_addr = address;
    qhn->next = NULL;
    /* Allocate QHead */
    qhn->qh = ps_dma_alloc_pinned(edev->dman, sizeof(*qh), 32, 0, PS_MEM_NORMAL, &qhn->pqh);
    assert(qhn->qh);
    qh = qhn->qh;
    /** Initialise QH **/
    qh->qhlptr = QHLP_INVALID;
    /* epc0 */
    switch (speed) {
    case USBSPEED_HIGH:
        qh->epc[0] = QHEPC0_HSPEED;
        break;
    case USBSPEED_FULL:
        qh->epc[0] = QHEPC0_FSPEED;
        break;
    case USBSPEED_LOW :
        qh->epc[0] = QHEPC0_LSPEED;
        break;
    default:
        usb_assert(0);
    }
    qh->epc[0] |= QHEPC0_MAXPKTLEN(max_pkt) | QHEPC0_ADDR(address) |
                  QHEPC0_EP(ep) | QHEPC0_NAKCNT_RL(0x8);
    qh->epc[0] |= QHEPC0_DTC;
    if (xact[0].type == PID_SETUP && speed != USBSPEED_HIGH) {
        qh->epc[0] |= QHEPC0_C;
    }
    /* epc1 */
    qh->epc[1] = QHEPC1_HUB_ADDR(hub_addr) | QHEPC1_PORT(hub_port) | QHEPC1_MULT(1);
    /* TD overlay */
    qh->td_cur = TDLP_INVALID;

    qh->td_overlay.token = 0;
    qh->td_overlay.next = TDLP_INVALID;
    qh->td_overlay.alt = TDLP_INVALID;

    /* Initialise all TDs */
    qhn->tdns = malloc(sizeof(*qhn->tdns) * qhn->ntdns);
    usb_assert(qhn->tdns);
    prev_td = &qh->td_overlay;
    for (i = 0; i < qhn->ntdns; i++) {
        /* Initialise TD */
        struct TD* td;
        uintptr_t ptd = 0;
        int err;
        td = ps_dma_alloc_pinned(edev->dman, sizeof(*td), 32, 0, PS_MEM_NORMAL, &ptd);
        usb_assert(td);
        td->next = TDLP_INVALID;
        td->alt = TDLP_INVALID;
        td->token = 0;
        err = td_set_buf(td, xact_get_paddr(&xact[i]), xact[i].len);
        usb_assert(!err);
        /* Transfer type */
        switch (xact[i].type) {
        case PID_INT  :
        case PID_IN   :
            td->token = TDTOK_PID_IN   ;
            break;
        case PID_OUT  :
            td->token = TDTOK_PID_OUT  ;
            break;
        case PID_SETUP:
            td->token = TDTOK_PID_SETUP;
            break;
        default:
            usb_assert(0);
        };
        /* Data toggle */
        if (xact[i].type == PID_SETUP) {
            dt = 0;
        }
        if (dt++ & 1 || (xact[0].type == PID_SETUP && i == nxact - 1)) {
            td->token |= TDTOK_DT;
        }
        /* etc */
        td->token |= TDTOK_BYTES(xact[i].len) |
                     TDTOK_C_ERR(3) |
                     TDTOK_SACTIVE;
        qhn->tdns[i].td = td;
        qhn->tdns[i].ptd = ptd;
        qhn->tdns[i].xact = xact[i];
        /* Link the previous TD */
        td->next = TDLP_INVALID;
        prev_td->next = ptd;
        prev_td = td;
    }

    /* Terminate the TD list and add IOC if requested */
    prev_td->token |= (cb) ? TDTOK_IOC : 0;

#if defined(DEBUG_DES)
    dump_qhn(qhn);
#endif
    return qhn;
}

static void
qhn_destroy(ps_dma_man_t* dman, struct QHn* qhn)
{
    int i;
#ifdef EHCI_TRAFFIC_DEBUG
    printf("Completed QH:\n");
    dump_qhn(qhn);
#endif
    for (i = 0; i < qhn->ntdns; i++) {
        ps_dma_free_pinned(dman, (void*)qhn->tdns[i].td, sizeof(*qhn->tdns[i].td));
    }
    ps_dma_free_pinned(dman, (void*)qhn->qh, sizeof(*qhn->qh));
    free(qhn->tdns);
    free(qhn);
}

/**************************
 **** Queue scheduling ****
 **************************/

static int
_new_periodic_schedule(struct ehci_host* edev, int size)
{
    uint32_t* list = NULL;
    uint32_t v;
    int i;

    usb_assert(size == 1024 || (edev->cap_regs->hccparams & EHCI_HCC_PFRAMELIST));

    /* Create and initialise an empty periodic frame list */
    edev->flist = ps_dma_alloc_pinned(edev->dman, size * sizeof(*list), 0x1000, 0, PS_MEM_NORMAL, &edev->pflist);
    usb_assert(((uintptr_t)edev->flist & 0xfff) == 0);
    if (!edev->flist) {
        usb_assert(list);
        return -1;
    }
    list = edev->flist;
    for (i = 0; i < size; i++) {
        list[i] = QHLP_INVALID;
    }
    /* Halt the controller before making these changes */
    edev->op_regs->usbcmd &= ~EHCICMD_RUNSTOP;
    while (!(edev->op_regs->usbsts & EHCISTS_HCHALTED));
    /* Attach the list to the edevice */
    edev->flist_size = size;
    edev->op_regs->periodiclistbase = edev->pflist;
    edev->op_regs->frindex = FRAME2UFRAME(0) | FRINDEX_UF(1);
    v = edev->op_regs->usbcmd & ~EHCICMD_LIST_SMASK;
    switch (edev->flist_size) {
    case  256:
        v |= EHCICMD_LIST_S256 ;
        break;
    case  512:
        v |= EHCICMD_LIST_S512 ;
        break;
    case 1024:
        v |= EHCICMD_LIST_S1024;
        break;
    default:
        usb_assert(!"Invalid periodic frame list size");
    }
    edev->op_regs->usbcmd = v;
    asm volatile("dmb");
    /* Enale interrutps */
    v = edev->op_regs->usbcmd & ~EHCICMD_IRQTHRES_MASK;
    v |= EHCICMD_IRQTHRES(0x1);
    edev->op_regs->usbcmd = v;
    /* Enable the list */
    asm volatile("dmb");
    edev->op_regs->usbcmd |= EHCICMD_PERI_EN;
    asm volatile("dmb");
    edev->op_regs->usbcmd |= EHCICMD_RUNSTOP;
    while (!(edev->op_regs->usbsts & EHCISTS_PERI_EN));
    /* And we are done! */
    return 0;
}

/**
 * Reset all host modified fields as appropriate ready for the
 * next INTerrupt. qhn->last_sched will be updated with the
 * position of the schedule. If the INT has missed its time
 * slot, it will be scheduled ASAP to avoid rollover delay.
 */
static void
_int_schedule(struct ehci_host* edev, struct QHn* qhn)
{
    volatile struct QH* qh;
    volatile struct TD* td;
    struct xact *xact;
    UNUSED int err;
    /* Pull out our pointers to the descriptors */
    qh = qhn->qh;
    td = qhn->tdns[0].td;
    xact = &qhn->tdns[0].xact;
    /* TODO we do not look at this granularity */
    qh->epc[1] &= ~QHEPC1_UFRAME_MASK;
    qh->epc[1] |= QHEPC1_UFRAME_SMASK(0b000001);
    qh->epc[1] |= QHEPC1_UFRAME_CMASK(0b011100);
    /* NAKCNT_RL must be 0 for INT packets (EHCI chapter 4.9) */
    qh->epc[0] &= ~QHEPC0_NAKCNT_RL_MASK;
    /* Refresh the TD overlay area */
    qh->td_cur = 0;
    qh->td_overlay.next = qhn->tdns[0].ptd;
    qh->td_overlay.alt = TDLP_INVALID;
    qh->td_overlay.token = 0;
    /* Refresh the TD token and buffer */
    td->token &= ~(TDTOK_ERROR);
    td->token |= TDTOK_C_ERR(3);
    err = td_set_buf(td, xact->paddr, xact->len);
    usb_assert(!err);
    /* Fence and activate the INT */
    dmb();
    td->token |= TDTOK_SACTIVE;
    /* Start the periodic schedule in case it is stopped */
    edev->op_regs->usbcmd |= EHCICMD_PERI_EN;
    edev->op_regs->usbcmd |= EHCICMD_RUNSTOP;
}

static void
_qhn_deschedule(struct ehci_host* dev, struct QHn* qhn)
{
    int i;
    /* TODO only supporting 1 int */
    for (i = 0; i < dev->flist_size; i++) {
        dev->flist[i] = QHLP_INVALID;
    }
}

static int
ehci_schedule_periodic_root(struct ehci_host* edev, struct xact *xact,
                            int nxact, usb_cb_t cb, void* t)
{
    int port;
    usb_assert(xact->vaddr);
    usb_assert(cb);
    edev->irq_xact = *xact;
    edev->irq_cb = cb;
    edev->irq_token = t;
    /* Enable IRQS */
    for (port = 1; port <= EHCI_HCS_N_PORTS(edev->cap_regs->hcsparams); port++) {
        volatile uint32_t* ps_reg = _get_portsc(edev, port);
        uint32_t v;
        v = *ps_reg & ~(EHCI_PORT_CHANGE);
        v |= (EHCI_PORT_WO_OCURRENT | EHCI_PORT_WO_DCONNECT | EHCI_PORT_WO_CONNECT);
        *ps_reg = v;
    }
    edev->op_regs->usbintr |= EHCIINTR_PORTC_DET;
    return 0;
}

static int
ehci_schedule_periodic(struct ehci_host* edev, struct QHn* qhn, int rate_ms)
{
    uint32_t sched;
    uint32_t *list;

    /* Create the DMA periodic frame list if required */
    if (edev->flist == NULL) {
        if (_new_periodic_schedule(edev, 1024)) {
            assert(0);
            return -1;
        }
    }

    /* Create the transaction */
    qhn->rate = rate_ms * (1000 / 125);

    /* Add it to the int list */
    qhn->next = edev->intn_list;
    edev->intn_list = qhn;

    /* Schedule the INT packet */
    _int_schedule(edev, qhn);

    /* Insert the INT into the schedule */
    list = edev->flist;
    for (sched = 0; sched < FRAME2UFRAME(edev->flist_size); sched += qhn->rate) {
        uint32_t frame = UFRAME2FRAME(sched);
        int offset = FRINDEX_UF(sched);
        /* TODO Currently only supporting 1 scheduled int */
        /* If the index is already in use, try the next */
        while (list[frame] != QHLP_INVALID) {
            frame++;
        }
        /* Add to the list if we can */
        if (list[frame] == QHLP_INVALID) {
            list[frame] = QHLP_TYPE_QH | qhn->pqh;
        }
        /* TODO uFrame is stored in the QH and we have only one of thes to we ignore the S/C-MASKS */
        (void)offset;
    }
    return 0;
}

static enum usb_xact_status
qhn_wait(struct QHn* qhn, int to_ms)
{
    enum usb_xact_status stat;
    do {
        stat = qhn_get_status(qhn);
        if (stat != XACTSTAT_PENDING) {
            break;
        } else if (to_ms-- == 0) {
            break;
        } else {
            msdelay(1);
        }
    } while (1);

    /* Check the result */
    if (to_ms < 0) {
        printf("USB timeout\n");
    }
    switch (stat) {
    case XACTSTAT_SUCCESS:
        break;
    case XACTSTAT_ERROR:
    case XACTSTAT_PENDING:
    case XACTSTAT_HOSTERROR:
    default:
        printf("Bad status %d\n", stat);
        dump_qhn(qhn);
    }
    return stat;
}

/* Remove from async schedule; turn the schedule off if it is empty */
static void
_async_remove_next(struct ehci_host* edev, struct QHn* prev)
{
    struct QHn* q = prev->next;
    if (prev == q) {
        _disable_async(edev);
        edev->alist_tail = NULL;
        edev->op_regs->asynclistaddr = 0;
    } else {
        /* Remove single node from asynch schedule */
        /* If we are removing the "Head", reassign it */
        if (q->qh->epc[0] & QHEPC0_H) {
            q->next->qh->epc[0] |= QHEPC0_H;
        }
        /* If we removed the tail, reassign it */
        if (q == edev->alist_tail) {
            edev->alist_tail = prev;
        }

        prev->qh->qhlptr = q->qh->qhlptr;
        prev->next = q->next;
    }
    /* Add to doorbell list for cleanup */
    q->next = edev->db_pending;
    edev->db_pending = q;
}

static int
ehci_schedule_async(struct ehci_host* edev, struct QHn* qh_new)
{
    struct QHn *qh_cur;
    if (edev->alist_tail) {
        /* Insert into list */
        qh_cur = edev->alist_tail;
        /* HeadNew.HorizontalPtr = pHeadCurrent.HorizontalPtr */
        qh_new->qh->qhlptr = qh_cur->qh->qhlptr;
        qh_new->next = qh_cur->next;
        /* pHeadCurrent.HorizontalPointer = paddr(pQueueHeadNew) */
        qh_cur->qh->qhlptr = qh_new->pqh | QHLP_TYPE_QH;
        qh_cur->next = qh_new;
    } else {
        /* New list */
        edev->alist_tail = qh_cur = qh_new;
        qh_new->qh->epc[0] |= QHEPC0_H;
        qh_new->qh->qhlptr = qh_new->pqh | QHLP_TYPE_QH;
        qh_new->next = qh_new;
        edev->op_regs->asynclistaddr = qh_new->pqh;
    }

    /* Enable the async schedule */
    _enable_async(edev);

    if (qh_new->cb == NULL) {
        enum usb_xact_status stat;
        uint32_t v;
        /* Wait for TDs to be processed. */
        stat = qhn_wait(qh_new, 3000);
        v = qhn_get_bytes_remaining(qh_new);
        _async_remove_next(edev, qh_cur);
        return (stat == XACTSTAT_SUCCESS) ? v : -1;
    } else {
        edev->alist_tail = qh_new;
        return 0;
    }
}

static void
_async_doorbell(struct ehci_host* edev)
{
    while (edev->db_active) {
        struct QHn* qhn;
        qhn = edev->db_active;
        edev->db_active = qhn->next;
        if (qhn->was_cancelled) {
            enum usb_xact_status stat;
            assert(qhn->cb);
            stat = qhn_get_status(qhn);
            if (stat != XACTSTAT_PENDING) {
                /* The transfer must have completed while it was being cancelled */
                qhn_cb(qhn, stat);
            } else {
                qhn_cb(qhn, XACTSTAT_CANCELLED);
            }
        }
        qhn_destroy(edev->dman, qhn);
    }
}


static void
_async_complete(struct ehci_host* edev)
{
    if (edev->alist_tail) {
        struct QHn *cur, *prev, *tail;
        /* We cache the tail because edev->alist_tail may change during node removal */
        prev = tail = edev->alist_tail;
        do {
            enum usb_xact_status stat;
            cur = prev->next;
            stat = qhn_get_status(cur);
            if (stat != XACTSTAT_PENDING) {
                if(stat == XACTSTAT_ERROR){
                    printf("--- <Transfer error> ---\n");
                    dump_qhn(cur);
                    printf("------------------------\n");
                }

                /* Call the completion handler and remove cur. prev is updated to point to a new cur */
                if (cur->cb) {
                    qhn_cb(cur, stat);
                    _async_remove_next(edev, prev);
                }
            } else {
                /* Step over */
                prev = cur;
            }
        } while (cur != tail);
    }
}

static void
_root_irq(struct ehci_host* edev)
{
    uint8_t *portbm;
    volatile uint32_t* psc;
    int nports;
    int port;
    int resched;
    /* Determine how many ports we should query */
    nports = EHCI_HCS_N_PORTS(edev->cap_regs->hcsparams);
    if (nports > edev->irq_xact.len * 8) {
        nports = edev->irq_xact.len * 8;
    }
    /* Set the INT data */
    usb_assert(edev->irq_xact.vaddr);
    usb_assert(edev->irq_cb);
    psc = _get_portsc(edev, 1);
    portbm = xact_get_vaddr(&edev->irq_xact);
    memset(portbm, 0, edev->irq_xact.len);
    /* Hub itself is at position 0 */
    for (port = 1; port <= nports; port++) {
        if (psc[port - 1] & EHCI_PORT_CHANGE) {
            portbm[port / 8] |= BIT(port & 0x7);
        }
        if (!(psc[port - 1] & EHCI_PORT_RESET) &&
                (edev->bmreset_c & BIT(port))) {
            portbm[port / 8] |= BIT(port & 0x7);
        }
    }
    /* Forward the IRQ */
    resched = edev->irq_cb(edev->irq_token, XACTSTAT_SUCCESS, 0);
    if (!resched) {
        usb_assert(0);
    }
}

static void
_periodic_complete(struct ehci_host* edev)
{
    struct QHn* qhn;
    struct QHn** qhn_ptr;
    qhn_ptr = &edev->intn_list;
    qhn = edev->intn_list;
    EHCI_IRQDBG(edev, "Scanning periodic list....\n");
    while (qhn != NULL) {
        if (!qhn->irq_pending) {
            /* Check the result */
            enum usb_xact_status stat = qhn_get_status(qhn);
            switch (stat) {
            case XACTSTAT_PENDING:
                break;
            case XACTSTAT_ERROR:
            case XACTSTAT_SUCCESS:
                qhn->irq_pending = 1;
#if defined(DEBUG_DES)
                dump_qhn(qhn);
#endif
                if (qhn_cb(qhn, stat)) {
                    _int_schedule(edev, qhn);
                    qhn->irq_pending = 0;
                } else {
                    struct QHn* cur;
                    cur = qhn;
                    qhn = cur->next;
                    *qhn_ptr = cur->next;
                    _qhn_deschedule(edev, cur);
                    qhn_destroy(edev->dman, cur);
                    continue;
                }
                break;
            case XACTSTAT_HOSTERROR:
            default:
                printf("Bad status %d\n", qhn_get_status(qhn));
                dump_qhn(qhn);
            }
        }
        qhn_ptr = &qhn->next;
        qhn = qhn->next;
    }
}

static int
ehci_schedule_xact(usb_host_t* hdev, uint8_t addr, int8_t hub_addr, uint8_t hub_port,
                   enum usb_speed speed, int ep, int max_pkt, int rate_ms,
                   int dt, struct xact* xact, int nxact, usb_cb_t cb, void* t)
{
    struct QHn *qhn;
    struct ehci_host* edev;
    usb_assert(hdev);
    edev = _hcd_to_ehci(hdev);
    if (hub_addr == -1) {
        /* Send off to root handler... No need to create QHn */
        if (rate_ms) {
            return ehci_schedule_periodic_root(edev, xact, nxact, cb, t);
        } else {
            return hubem_process_xact(edev->hubem, ep, xact, nxact, cb, t);
        }
    }
    /* Create the QHn */
    qhn = qhn_new(edev, addr, hub_addr, hub_port, speed, ep, max_pkt,
                  dt, xact, nxact, cb, t);
    if (qhn == NULL) {
        return -1;
    }
    /* Send off over the bus */
#ifdef  EHCI_TRAFFIC_DEBUG
    printf("%s schedule:\n", (rate_ms) ? "Periodic" : "Async");
    dump_qhn(qhn);
#endif
    if (rate_ms) {
        return ehci_schedule_periodic(edev, qhn, rate_ms);
    } else {
        return ehci_schedule_async(edev, qhn);
    }
}

static void
check_doorbell(struct ehci_host* edev)
{
    if (_is_enabled_async(edev)) {
        if (edev->db_active == NULL && edev->db_pending != NULL) {
            /* Ring the bell */
            edev->db_active = edev->db_pending;
            edev->db_pending = NULL;
            edev->op_regs->usbcmd |= EHCICMD_ASYNC_DB;
        }
    } else {
        /* Clean up all dangling transactions */
        _async_doorbell(edev);
        edev->db_active = edev->db_pending;
        edev->db_pending = NULL;
        _async_doorbell(edev);
    }
}

static void
ehci_handle_irq(usb_host_t* hdev)
{
    struct ehci_host* edev = _hcd_to_ehci(hdev);
    uint32_t sts;
    sts = edev->op_regs->usbsts;
    sts &= edev->op_regs->usbintr;
    if (sts & EHCISTS_HOST_ERR) {
        EHCI_IRQDBG(edev, "INT - host error\n");
        edev->op_regs->usbsts = EHCISTS_HOST_ERR;
        sts &= ~EHCISTS_HOST_ERR;
        _periodic_complete(edev);
        _async_complete(edev);
    }
    if (sts & EHCISTS_USBINT) {
        EHCI_IRQDBG(edev, "INT - USB\n");
        edev->op_regs->usbsts = EHCISTS_USBINT;
        sts &= ~EHCISTS_USBINT;
        _periodic_complete(edev);
        _async_complete(edev);
    }
    if (sts & EHCISTS_FLIST_ROLL) {
        EHCI_IRQDBG(edev, "INT - Frame list roll over\n");
        edev->op_regs->usbsts = EHCISTS_FLIST_ROLL;
        sts &= ~EHCISTS_FLIST_ROLL;
    }

    if (sts & EHCISTS_USBERRINT) {
        EHCI_IRQDBG(edev, "INT - USB error\n");
        edev->op_regs->usbsts = EHCISTS_USBERRINT;
        sts &= ~EHCISTS_USBERRINT;
        _async_complete(edev);
        _periodic_complete(edev);
    }
    if (sts & EHCISTS_PORTC_DET) {
        EHCI_IRQDBG(edev, "INT - root hub port change\n");
        edev->op_regs->usbsts = EHCISTS_PORTC_DET;
        sts &= ~EHCISTS_PORTC_DET;
        _root_irq(edev);
    }
    if (sts & EHCISTS_ASYNC_ADV) {
        EHCI_IRQDBG(edev, "INT - async list advance\n");
        edev->op_regs->usbsts = EHCISTS_ASYNC_ADV;
        sts &= ~EHCISTS_ASYNC_ADV;
        _async_doorbell(edev);
    }
    if (sts) {
        printf("Unhandled USB irq. Status: 0x%x\n", sts);
        usb_assert(!"Unhandled irq");
    }

    check_doorbell(edev);
}

static int
clear_periodic_xact(struct ehci_host* edev, void* token)
{
    struct QHn** qhn_ptr;
    struct QHn* qhn;
    /* Clear from periodic list */
    qhn_ptr = &edev->intn_list;
    qhn = edev->intn_list;
    while (qhn != NULL) {
        if (qhn->token == token) {
            _qhn_deschedule(edev, qhn);
            /* Process and remove the QH node */
            qhn_cb(qhn, XACTSTAT_CANCELLED);
            *qhn_ptr = qhn->next;
            qhn_destroy(edev->dman, qhn);
            qhn = *qhn_ptr;
            return 0;
        } else {
            qhn_ptr = &qhn->next;
            qhn = qhn->next;
        }
    }
    return -1;
}

static int
clear_async_xact(struct ehci_host* edev, void* token)
{
    /* Clear from the async list. */
    if (edev->alist_tail) {
        struct QHn *prev, *cur, *tail;
        /* We cache the tail due to distructive updated within the loop */
        prev = tail = edev->alist_tail;
        do {
            cur = prev->next;
            assert(cur != NULL);
            if (cur->token == token) {
                /* Remove it. The doorbell will notify the client */
                cur->was_cancelled = 1;
                _async_remove_next(edev, prev);
                return 0;
            } else {
                prev = cur;
            }
        } while (cur != tail);
    }
    return 1;
}

static int
ehci_cancel_xact(usb_host_t* hdev, void * token)
{
    struct ehci_host* edev = _hcd_to_ehci(hdev);
    if (token != NULL) {
        int err;
        /* Clear from periodic schedule */
        err = clear_periodic_xact(edev, token);
        if (!err) {
            return 0;
        }

        /* Clear from async schedule */
        err = clear_async_xact(edev, token);
        if (!err) {
            /* Cancel is not called from the ISR. Ring the bell or finalise heads. */
            check_doorbell(edev);
            return 0;
        }
        EHCI_DBG(edev, "Unable to find transaction for removal (0x%x)\n", (uint32_t)token);
    }
    return -1;
}


/****************************
 **** Exported functions ****
 ****************************/
int
ehci_host_init(usb_host_t* hdev, uintptr_t regs,
               void (*board_pwren)(int port, int state))
{
    usb_hubem_t hubem;
    struct ehci_host* edev;
    int pwr_delay_ms;
    uint32_t v;
    int err;
    hdev->pdata = (struct usb_hc_data*)malloc(sizeof(struct usb_hc_data));
    if (hdev->pdata == NULL) {
        return -1;
    }
    edev = _hcd_to_ehci(hdev);
    edev->devid = hdev->id;
    edev->cap_regs = (volatile struct ehci_host_cap*)regs;
    edev->op_regs = (volatile struct ehci_host_op*)(regs + edev->cap_regs->caplength);
    hdev->schedule_xact = ehci_schedule_xact;
    hdev->cancel_xact = ehci_cancel_xact;
    hdev->handle_irq = ehci_handle_irq;
    edev->board_pwren = board_pwren;

    /* Check some params */
    hdev->nports = EHCI_HCS_N_PORTS(edev->cap_regs->hcsparams);
    assert(usb_hcd_count_ports(hdev) > 0);
    assert(usb_hcd_count_ports(hdev) < 32);
    edev->bmreset_c = 0;
    usb_assert(!(edev->cap_regs->hccparams & EHCI_HCC_64BIT));

    /* Make sure we are halted before before reset */
    edev->op_regs->usbcmd &= ~EHCICMD_RUNSTOP;
    while (!(edev->op_regs->usbsts & EHCISTS_HCHALTED));
    /* Reset the HC */
    edev->op_regs->usbcmd |= EHCICMD_HCRESET;
    while (edev->op_regs->usbcmd & EHCICMD_HCRESET);

    /* Initialise the hub emulation */
    pwr_delay_ms = 100; /* Sample value from real hub */
    err = usb_hubem_driver_init(edev, hdev->nports, pwr_delay_ms,
                                &_set_pf, &_clr_pf, &_get_pstat,
                                &hubem);
    if (err) {
        usb_assert(0);
        return -1;
    }
    edev->hubem = hubem;
    edev->dman = hdev->dman;
    /* Terminate the periodic schedule head */
    edev->alist_tail = NULL;
    edev->db_pending = NULL;
    edev->db_active = NULL;
    edev->flist = NULL;
    edev->intn_list = NULL;
    /* Initialise IRQ */
    edev->irq_cb = NULL;
    edev->irq_token = NULL;
    edev->irq_xact.vaddr = NULL;
    edev->irq_xact.len = 0;

    /* Initialise the controller. */
    v = edev->op_regs->usbcmd;
    v &= ~(EHCICMD_LIGHT_RST | EHCICMD_ASYNC_DB |
           EHCICMD_PERI_EN | EHCICMD_ASYNC_EN |
           EHCICMD_HCRESET);
    v |= EHCICMD_RUNSTOP;
    edev->op_regs->usbcmd = v;
    edev->op_regs->configflag |= EHCICFLAG_CFLAG;
    dsb();
    msdelay(5);

    /* Enable Interrupts */
    v = edev->op_regs->usbintr;
    v |= EHCIINTR_HOST_ERR | EHCIINTR_USBERRINT
         | EHCIINTR_USBINT | EHCIINTR_ASYNC_ADV;
    edev->op_regs->usbintr = v;

    return 0;
}

