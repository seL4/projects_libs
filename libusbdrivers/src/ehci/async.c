/*
 * Copyright 2016, NICTA
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(NICTA_BSD)
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <usb/drivers/usbhub.h>
#include "ehci.h"
#include "../services.h"

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

enum usb_xact_status
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

enum usb_xact_status
qhn_get_status(struct QHn * qhn)
{
    int i;
    struct TDn *tdn;

    tdn = qhn->tdns;
    while (tdn) {
            enum usb_xact_status stat;
            stat = qtd_get_status(tdn->td);
            if (stat != XACTSTAT_SUCCESS) {
                return stat;
            }
	    tdn = tdn->next;
    }
    /* All TDs complete, return the status of the QH */
    return qtd_get_status(&qhn->qh->td_overlay);
}

static inline int
qhn_get_bytes_remaining(struct QHn *qhn)
{
    int sum = 0;
    int i;

    for (i = 0; i < qhn->ntdns; i++) {
       sum += TDTOK_GET_BYTES(qhn->tdns->td->token);
    }

    return sum;
}

int
qhn_cb(struct QHn *qhn, enum usb_xact_status stat)
{
    return qhn->cb(qhn->token, stat, qhn_get_bytes_remaining(qhn));
}

/****************************
 **** Queue manipulation ****
 ****************************/

/*
 * TODO: The current data structure assumes one xact per TD, which means the
 * length of an xact can not exceed 20KB.
 */
struct TDn*
qtd_alloc(struct ehci_host *edev, enum usb_speed speed, struct endpoint *ep,
		struct xact *xact, int nxact)
{
	struct TDn *head_tdn, *prev_tdn, *tdn;
	int buf_filled, cnt, total_bytes = 0;
	int xact_stage = 0;

	assert(xact);
	assert(nxact > 0);

	head_tdn = calloc(1, sizeof(struct TDn) * nxact);
	prev_tdn = NULL;
	for (int i = 0; i < nxact; i++) {
		tdn = head_tdn + sizeof(struct TDn) * i;

		/* Allocate TD overlay */
		tdn->td = ps_dma_alloc_pinned(edev->dman, sizeof(*tdn->td), 32, 0,
				PS_MEM_NORMAL, &tdn->ptd);
		assert(tdn->td);
		memset((void*)tdn->td, 0, sizeof(*tdn->td));

		/* Fill in the TD */
		if (prev_tdn) {
			prev_tdn->td->next = tdn->ptd;
		}
		tdn->td->alt = TDLP_INVALID;

		/* The Control endpoint manages its own data toggle */
		if (ep->type == EP_CONTROL) {
			if (ep->max_pkt & (ep->max_pkt + total_bytes - 1)) {
				tdn->td->token = TDTOK_DT;
			}
		}
		tdn->td->token |= TDTOK_BYTES(xact[i].len);
		tdn->td->token |= TDTOK_C_ERR(0x3); //Maximize retries

		switch (xact[i].type) {
			case PID_SETUP:
				tdn->td->token |= TDTOK_PID_SETUP;
				xact_stage |= TDTOK_PID_SETUP;
				break;
			case PID_IN:
				tdn->td->token |= TDTOK_PID_IN;
				xact_stage |= TDTOK_PID_IN;
				break;
			case PID_OUT:
				tdn->td->token |= TDTOK_PID_OUT;
				xact_stage |= TDTOK_PID_OUT;
				break;
			default:
				assert("Invalid PID!\n");
				break;
		}

		tdn->td->token |= TDTOK_SACTIVE;

		/* Ping control */
		if (speed == USBSPEED_HIGH && xact[i].type == PID_OUT) {
			tdn->td->token |= TDTOK_PINGSTATE;
		}

		/* Fill in the buffer */
		cnt = 0;
		tdn->td->buf[cnt] = xact[i].paddr; //First buffer has offset
		buf_filled = 0x1000 - (xact[i].paddr & 0xFFF);
		/* All following buffers are page aligned */
		while (buf_filled < xact[i].len) {
			cnt++;
			tdn->td->buf[cnt] = (xact[i].paddr + 0x1000 * cnt) & ~0xFFF;
			buf_filled += 0x1000;
		}
		assert(cnt <= 4); //We only have 5 page-sized buffers

		/* Total data transferred */
		total_bytes += xact[i].len;

		if (prev_tdn) {
			prev_tdn->next = tdn;
		}
		prev_tdn = tdn;
	}

	/*
	 * Zero length packet
	 * XXX: It is unclear that the exact condition of when the zero length
	 * packet is required. The following implementation is partially based
	 * on observation. According to USB 2.0 spec(5.5.3), a zero length
	 * packet shouldn't be needed under some of the situations below, but
	 * apparently, some devices don't always follow the spec.
	 */
	if (((xact_stage & TDTOK_PID_OUT) && !(total_bytes % ep->max_pkt)) ||
			ep->type == EP_CONTROL) {
		/* Allocate TD for the zero length packet */
		tdn = calloc(1, sizeof(struct TDn));

		/* Allocate TD overlay */
		tdn->td = ps_dma_alloc_pinned(edev->dman, sizeof(*tdn->td),
				32, 0, PS_MEM_NORMAL, &tdn->ptd);
		assert(tdn->td);
		memset((void*)tdn->td, 0, sizeof(*tdn->td));

		/* Fill in the TD */
		tdn->td->alt = TDLP_INVALID;
		tdn->td->token = TDTOK_C_ERR(0x3) | TDTOK_SACTIVE;

		if (xact_stage & TDTOK_PID_SETUP) {
			/* Flip the PID, if there is no data stage, then IN */
			if (xact_stage & TDTOK_PID_IN) {
				tdn->td->token |= TDTOK_PID_OUT;
			} else {
				tdn->td->token |= TDTOK_PID_IN;
			}

			/* Force DATA1 */
			tdn->td->token |= TDTOK_DT;
		} else {
			/* Bulk out transfer */
			tdn->td->token |= TDTOK_PID_OUT;

			/* XXX: Flip the data toggle? */
			if (!(prev_tdn->td->token & TDTOK_DT)) {
				tdn->td->token |= TDTOK_DT;
			}
		}

		/* Add to the list */
		prev_tdn->td->next = tdn->ptd;
		prev_tdn->next = tdn;
	}

	/* Send IRQ when finished processing the last TD */
	tdn->td->token |= TDTOK_IOC;

	/* Mark the last TD as terminate TD */
	tdn->td->next |= TDLP_INVALID;

	return head_tdn;
}

/*
 * Allocate generic queue head for both periodic and asynchronous schedule
 * Note that the link pointer and reclamation flag bit are set when inserting
 * the queue head to asynchronous schedule.
 *
 * XXX: For some unknown reason, we cannot initialize the link pointer and head
 * type here, it seems that the register can be only written once. So we'll fill
 * the register when adding the queue head to the schedule.
 */
struct QHn*
qhn_alloc(struct ehci_host *edev, uint8_t address, uint8_t hub_addr,
	  uint8_t hub_port, enum usb_speed speed, struct endpoint *ep)
{
	struct QHn *qhn;
	volatile struct QH  *qh;

	qhn = calloc(1, sizeof(struct QHn));
	assert(qhn);

	/* Allocate queue head */
	qhn->qh = ps_dma_alloc_pinned(edev->dman, sizeof(*qh), 32, 0,
			PS_MEM_NORMAL, &qhn->pqh);
	assert(qhn->qh);
	memset((void*)qhn->qh, 0, sizeof(*qh));

	/* Fill in the queue head */
	qh = qhn->qh;

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

	qh->epc[0] |= QHEPC0_MAXPKTLEN(ep->max_pkt) | QHEPC0_ADDR(address) |
		      QHEPC0_EP(ep->num);

	/*
	 * Nak counter must NOT be used for interrupt endpoint
	 * EHCI spec chapter 4.9(Nak "Not Used" mode)
	 */
	if (ep->type == EP_INTERRUPT) {
		qh->epc[0] |= QHEPC0_NAKCNT_RL(0);
	} else {
		qh->epc[0] |= QHEPC0_NAKCNT_RL(0x8);
	}

	/* Control endpoint manages its own data toggle */
	if (ep->type == EP_CONTROL) {
		qh->epc[0] |= QHEPC0_DTC;

		/* For full/low speed control endpoint */
		if (speed != USBSPEED_HIGH) {
			qh->epc[0] |= QHEPC0_C;
		}
	}

	if ((ep->type == EP_INTERRUPT || ep->type == EP_ISOCHRONOUS) &&
			speed != USBSPEED_HIGH) {
		qh->epc[0] |= QHEPC0_I;
	}

	/* epc1 */
	qh->epc[1] = QHEPC1_MULT(1);
	if (speed != USBSPEED_HIGH) {
		qh->epc[1] |= QHEPC1_HUB_ADDR(hub_addr) | QHEPC1_PORT(hub_port);
	}

	/* TODO: Check CMASK and SMASK */
	if ((ep->type == EP_INTERRUPT || ep->type == EP_ISOCHRONOUS) &&
			speed != USBSPEED_HIGH) {
		qh->epc[1] |= QHEPC1_UFRAME_CMASK(0x1C);
	}

	if (ep->type == EP_INTERRUPT) {
		qh->epc[1] |= QHEPC1_UFRAME_SMASK(1);
	}
	
	qh->td_overlay.next = TDLP_INVALID;
	qh->td_overlay.alt= TDLP_INVALID;

	return qhn;
}

void
qhn_update(struct QHn *qhn, uint8_t address, struct endpoint *ep)
{
	uint32_t epc0;

	assert(qhn);
	assert(ep);

	/*
	 * We only care about the control endpoint, because all other
	 * endpoints' info is extracted from the endpoint descriptor, and by the
	 * time the core driver reads the descriptors, the device's address
	 * should have settled already.
	 */
	if (ep->type != EP_CONTROL) {
		return;
	}

	/* Update maximum packet size */
	epc0 = qhn->qh->epc[0];
	if (unlikely(QHEPC0_GET_MAXPKT(epc0) != ep->max_pkt)) {
		epc0 &= ~QHEPC0_MAXPKT_MASK;
		epc0 |= QHEPC0_MAXPKTLEN(ep->max_pkt);
	}

	/* Update device address */
	if (unlikely(QHEPC0_GET_ADDR(epc0) != address)) {
		epc0 &= ~QHEPC0_ADDR_MASK;
		epc0 |= QHEPC0_ADDR(address);
	}

	qhn->qh->epc[0] = epc0;
}

void
qtd_enqueue(struct ehci_host *edev, struct QHn *qhn, struct TDn *tdn)
{
	struct TDn *last_tdn;

	assert(qhn);
	assert(tdn);

	/* If the queue is empty, point the TD overlay to the first TD */
	if (!qhn->tdns) {
		qhn->qh->td_overlay.next = tdn->ptd;
		qhn->tdns = tdn;
	} else {
		/* Find the last TD */
		last_tdn = qhn->tdns;
		while (last_tdn->next) {
			last_tdn = last_tdn->next;
		}

		/* Add new TD to the queue and update the termination bit */
		last_tdn->next = tdn;
		last_tdn->td->next = tdn->ptd & ~TDLP_INVALID;
	}
		qhn->qh->td_overlay.next = tdn->ptd;
}

void
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

int
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

void
_async_complete(struct ehci_host* edev)
{
	return;
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

void ehci_add_qhn_async(struct ehci_host *edev, struct QHn *qhn)
{
    /* Add new queue head to async queue */
    if (edev->alist_tail) {
	    /* Update the hardware queue */
	    qhn->qh->qhlptr = edev->alist_tail->qh->qhlptr;
	    edev->alist_tail->qh->qhlptr = qhn->pqh | QHLP_TYPE_QH;

	    /* Update the Software queue */
	    qhn->next = edev->alist_tail->next;
	    edev->alist_tail->next = qhn;
	    edev->alist_tail = qhn;
    } else {
	    edev->alist_tail = qhn;
	    edev->alist_tail->next = qhn;

	    qhn->qh->qhlptr = qhn->pqh | QHLP_TYPE_QH;
    }
}

/* TODO: Is it okay to use alist_tail and remove qhn */
int
ehci_schedule_async(struct ehci_host* edev, struct QHn* qhn)
{
	/* Make sure we are safe to write to the register */
	while (((edev->op_regs->usbsts & EHCISTS_ASYNC_EN) >> 15)
		^ ((edev->op_regs->usbcmd & EHCICMD_ASYNC_EN) >> 5));

	/* Enable the async scheduling */
	if (!(edev->op_regs->usbsts & EHCISTS_ASYNC_EN)) {
		qhn->qh->epc[0] |= QHEPC0_H;
		edev->op_regs->asynclistaddr = qhn->pqh;
		edev->op_regs->usbcmd |= EHCICMD_ASYNC_EN;
		while (edev->op_regs->usbsts & EHCISTS_ASYNC_EN) break;
	}

	struct TDn *tdn;
	int status;
	int cnt, sum = 0;

	tdn = qhn->tdns;
	while (tdn) {
		cnt = 3000;
		do {
			status = tdn->td->token & 0xFF;
			if (status == 0 || status == 1) {
				sum += TDTOK_GET_BYTES(tdn->td->token);
				break;
			}
			if (cnt <= 0) {
				printf("Timeout(%p, %p)\n", tdn->td, tdn->ptd);
				break;
			}
			msdelay(1);
			cnt--;
		} while (status);
		tdn = tdn->next;
	}

	qhn->tdns = NULL;
	qhn->ntdns = 0;

	return sum;
}

void
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

/* Remove from async schedule; turn the schedule off if it is empty */
void
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
void
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

