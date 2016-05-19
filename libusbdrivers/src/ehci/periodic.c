/*
 * Copyright 2016, NICTA
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(NICTA_BSD)
 */

#include "../services.h"
#include "ehci.h"

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
void
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

void
_qhn_deschedule(struct ehci_host* dev, struct QHn* qhn)
{
    int i;
    /* TODO only supporting 1 int */
    for (i = 0; i < dev->flist_size; i++) {
        dev->flist[i] = QHLP_INVALID;
    }
}

int
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

int
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

enum usb_xact_status
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


void
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

int
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

