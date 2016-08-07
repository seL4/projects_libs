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
	/* Make sure we are safe to write to the register */
	while (((edev->op_regs->usbsts & EHCISTS_PERI_EN) >> 14)
		^ ((edev->op_regs->usbcmd & EHCICMD_PERI_EN) >> 4));

	/* If the async scheduling is already enabled, do nothing */
	if (edev->op_regs->usbsts & EHCISTS_PERI_EN) {
	} else {
		/* Enable the async scheduling */
		edev->op_regs->periodiclistbase= edev->pflist;

		/* TODO: Check FRINDEX, FLIST_SIZE, IRQTHRES_MASK */
		edev->op_regs->usbcmd |= EHCICMD_PERI_EN;
		while (edev->op_regs->usbsts & EHCISTS_PERI_EN) break;
	}
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
		qhn->qh->qhlptr = QHLP_INVALID;
                if (!qhn_cb(qhn, stat)) {
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

