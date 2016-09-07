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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include <usb/usb_host.h>
#include <usb/drivers/usbhub.h>

#define EHCI_DEBUG_IRQ
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
#define EHCI_HCC_EECP(x)       (((x) >> 8) & 0xff)
#define EHCI_HCC_ISOTHRES(x)   (((x) >> 4) & 0xf)
#define EHCI_HCC_PARK          BIT(2)
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
    uint32_t buf_hi[5];        /* 64-bit capability(Appendix B) */
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
#define QHEPC0_MAXPKT_MASK     QHEPC0_MAXPKTLEN(0x7ff)
#define QHEPC0_GET_MAXPKT(x)   (((x) & QHEPC0_MAXPKT_MASK) >> 16)
#define QHEPC0_H               BIT(15)
#define QHEPC0_DTC             BIT(14)
#define QHEPC0_FSPEED          (0 * BIT(12))
#define QHEPC0_LSPEED          (1 * BIT(12))
#define QHEPC0_HSPEED          (2 * BIT(12))
#define QHEPC0_EP(x)           (((x) &  0xf) * BIT( 8))
#define QHEPC0_I               BIT(7)
#define QHEPC0_ADDR(x)         (((x) & 0x7f) * BIT( 0))
#define QHEPC0_ADDR_MASK       QHEPC0_ADDR(0x7f)
#define QHEPC0_GET_ADDR(x)     ((x) & QHEPC0_ADDR_MASK)
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
    usb_cb_t cb;
    void* token;
    struct TDn* next;
};

struct QHn {
    /* Transaction data */
    volatile struct QH* qh;
    uintptr_t pqh;
    int ntdns;
    struct TDn* tdns;
    /* Interrupts */
    int rate;
    usb_cb_t cb;      //TODO: In TDn now, to be removed.
    void* token;      //TODO: In TDn now, to be removed.
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

/**
 * Hub Emulation
 */
volatile uint32_t* _get_portsc(struct ehci_host* h, int port);
int _set_pf(void *token, int port, enum port_feature feature);
int _clr_pf(void *token, int port, enum port_feature feature);
int _get_pstat(void* token, int port, struct port_status* _ps);

/**
 * Async Scheduling
 */
void ehci_handle_irq(usb_host_t* hdev);
int ehci_cancel_xact(usb_host_t* hdev, void * token);

void qhn_destroy(ps_dma_man_t* dman, struct QHn* qhn);
int clear_async_xact(struct ehci_host* edev, void* token);
void _async_complete(struct ehci_host* edev);
int ehci_wait_for_completion(struct TDn *tdn);
void ehci_schedule_async(struct ehci_host* edev, struct QHn* qh_new);
void _async_doorbell(struct ehci_host* edev);
enum usb_xact_status qtd_get_status(volatile struct TD* qtd);
enum usb_xact_status qhn_get_status(struct QHn * qhn);
int qhn_cb(struct QHn *qhn, enum usb_xact_status stat);
void check_doorbell(struct ehci_host* edev);

/* New APIs */
struct QHn* qhn_alloc(struct ehci_host *edev, uint8_t address, uint8_t hub_addr,
	  uint8_t hub_port, enum usb_speed speed, struct endpoint *ep);
struct TDn* qtd_alloc(struct ehci_host *edev, enum usb_speed speed,
		struct endpoint *ep, struct xact *xact, int nxact,
		usb_cb_t cb, void *token);
void qhn_update(struct QHn *qhn, uint8_t address, struct endpoint *ep);
void qtd_enqueue(struct ehci_host *edev, struct QHn *qhn, struct TDn *tdn);
void ehci_add_qhn_async(struct ehci_host *edev, struct QHn *qhn);
void ehci_add_qhn_periodic(struct ehci_host *edev, struct QHn *qhn);

/**
 * Periodic Scheduling
 */
int ehci_schedule_periodic_root(struct ehci_host* edev, struct xact *xact,
                            int nxact, usb_cb_t cb, void* t);
int ehci_schedule_periodic(struct ehci_host* edev, struct QHn* qhn, int rate_ms);
enum usb_xact_status qhn_wait(struct QHn* qhn, int to_ms);
void _periodic_complete(struct ehci_host* edev);
int clear_periodic_xact(struct ehci_host* edev, void* token);
void _qhn_deschedule(struct ehci_host* dev, struct QHn* qhn);
void _async_remove_next(struct ehci_host* edev, struct QHn* prev);

/**
 * Debugging
 */
void dump_qtd(volatile struct TD* qtd);
void dump_qhn(struct QHn* qhn);
void dump_q(struct QHn* qhn);
void dump_edev(struct ehci_host* edev);

/**
 * Initialise a EHCI host controller
 * @param[in/out] hdev        A host controller structure to
 *                            populate. Must be pre-filled with a
 *                            DMA allocator. This function will
 *                            fill the private data and function
 *                            pointers of this structure.
 * @param[in]     cap_regs    memory location of the mapped ECHI
 *                            capability registers
 * @param[int]    board_pwren Function to call when power on/off
 *                            a port. Generally the PHY will take
 *                            care of this, but in cases where there
 *                            is no PHY (HSIC), a GPIO, etc may need
 *                            to be manually controlled.
 * @return                    0 on success
 */
int ehci_host_init(usb_host_t* hdev, uintptr_t cap_regs,
                   void (*board_pwren)(int port, int state));



#endif /* _EHCI_EHCI_H_ */
