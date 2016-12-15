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

/*
 * TODO: We only support interrupt endpoint at the moment, this function is
 * subject to change when we add isochronous endpoint support.
 */
void ehci_add_qhn_periodic(struct ehci_host *edev, struct QHn *qhn)
{
	struct QHn *last_qhn;

	/* Allocate the frame list */
	if (!edev->flist) {
		/* XXX: The frame list size is default to 1024 */
		edev->flist_size = 1024;
		edev->flist = ps_dma_alloc_pinned(edev->dman,
				edev->flist_size * sizeof(uint32_t*), 0x1000, 0,
				PS_MEM_NORMAL, &edev->pflist);
		usb_assert(edev->flist);

		/* Mark all frames as disabled */
		for (int i = 0; i < edev->flist_size; i++) {
			edev->flist[i] = TDLP_INVALID;
		}
	}

	/* Find an empty slot and insert the queue head */
	for (int i = 0; i < edev->flist_size; i++) {
		/*
		 * FIXME: We disable an interrupt ep by setting the TDLP_INVALID
		 * of the frame list, there is a race here.
		 */
		if (edev->flist[i] & TDLP_INVALID) {
			edev->flist[i] = qhn->pqh | QHLP_TYPE_QH;
			break;
		}
	}

	/* Add new queue head to the software queue */
	if (edev->intn_list) {
		/* Find the last queue head */
		last_qhn = edev->intn_list;
		while (last_qhn->next) {
		    last_qhn = last_qhn->next;
		}

		/* Add queue head to the list */
		last_qhn->next = qhn;
		/* TODO: Do we really need this line? */
		last_qhn->qh->qhlptr = qhn->pqh | QHLP_TYPE_QH;
	} else {
		edev->intn_list = qhn;
	}
}

void ehci_del_qhn_periodic(struct ehci_host *edev, struct QHn *qhn)
{
	struct QHn *prev, *cur;
	struct TDn *tdn, *tmp;

	/* Remove from the frame list */
	for (int i = 0; i < edev->flist_size; i++) {
		if (edev->flist[i] == (qhn->pqh | QHLP_TYPE_QH)) {
			edev->flist[i] = TDLP_INVALID;
			break;
		}
	}

	/* Remove from the software queue */
	prev = edev->intn_list;
	cur = prev;
	while (cur != NULL) {
		if (cur == qhn) {
			if (qhn->next) {
				prev->qh->qhlptr = qhn->qh->qhlptr;
				prev->next = qhn->next;
			} else {
				prev->qh->qhlptr = QHLP_INVALID;
				prev->next = NULL;
			}
			tdn = qhn->tdns;
			while (tdn) {
				tmp = tdn;
				tdn = tdn->next;
				if (tmp->cb) {
					tmp->cb(tmp->token, XACTSTAT_CANCELLED, 0);
				}
				ps_dma_free_pinned(edev->dman, (void*)tmp->td,
						sizeof(struct TD));
				free(tmp);
			}

			ps_dma_free_pinned(edev->dman, (void*)qhn->qh, sizeof(struct QH));
			free(qhn);
			break;
		}
		prev = cur;
		cur = cur->next;
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
ehci_schedule_periodic(struct ehci_host* edev)
{
	/* Make sure we are safe to write to the register */
	while (((edev->op_regs->usbsts & EHCISTS_PERI_EN) >> 14)
		^ ((edev->op_regs->usbcmd & EHCICMD_PERI_EN) >> 4));

	/* Enable the periodic schedule */
	if (!(edev->op_regs->usbsts & EHCISTS_PERI_EN)) {
		edev->op_regs->periodiclistbase= edev->pflist;

		/* TODO: Check FRINDEX, FLIST_SIZE, IRQTHRES_MASK */
		edev->op_regs->usbcmd |= EHCICMD_PERI_EN;
		while (edev->op_regs->usbsts & EHCISTS_PERI_EN) break;
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

void ehci_periodic_complete(struct ehci_host *edev)
{
	struct QHn *qhn;
	struct TDn *tdn;
	int sum;

	qhn = edev->intn_list;

	while (qhn) {
		tdn = qhn->tdns;
		if (qtd_get_status(tdn->td) == XACTSTAT_SUCCESS) {
			qhn->tdns = NULL;
			sum = TDTOK_GET_BYTES(tdn->td->token);
			if (tdn->cb) {
				tdn->cb(tdn->token, XACTSTAT_SUCCESS, sum);
			}
			ps_dma_free_pinned(edev->dman, (void*)tdn->td, sizeof(struct TD));
			free(tdn);
		}
		qhn = qhn->next;
	}
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

