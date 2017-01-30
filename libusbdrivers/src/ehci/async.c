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

/****************************
 **** Queue manipulation ****
 ****************************/
/*
 * TODO: The current data structure assumes one xact per TD, which means the
 * length of an xact can not exceed 20KB.
 */
struct TDn*
qtd_alloc(struct ehci_host *edev, enum usb_speed speed, struct endpoint *ep,
		struct xact *xact, int nxact, usb_cb_t cb, void *token)
{
	struct TDn *head_tdn = NULL, *prev_tdn, *tdn = NULL;
	int buf_filled, cnt, total_bytes = 0;
	int xact_stage = 0;

	assert(xact);
	assert(nxact > 0);

	prev_tdn = NULL;
	for (int i = 0; i < nxact; i++) {
		tdn = calloc(1, sizeof(struct TDn));
		assert(tdn);

		/* Allocate TD overlay */
		tdn->td = ps_dma_alloc_pinned(edev->dman, sizeof(*tdn->td), 32, 0,
				PS_MEM_NORMAL, &tdn->ptd);
		assert(tdn->td);
		memset((void*)tdn->td, 0, sizeof(*tdn->td));

		/* Fill in the TD */
		if (prev_tdn) {
			prev_tdn->td->next = tdn->ptd;
		} else {
			head_tdn = tdn;
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

		tdn->td->token |= TDTOK_SHALTED;

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
		tdn->td->token = TDTOK_C_ERR(0x3) | TDTOK_SHALTED;

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
	tdn->td->token |= TDTOK_IOC;   //TODO: Maybe disable IRQ when cb == NULL
	tdn->cb = cb;
	tdn->token = token;

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
		usb_sync_lock(edev->sync, qhn->lock);

		/* Find the last TD */
		last_tdn = qhn->tdns;
		while (last_tdn->next) {
			last_tdn = last_tdn->next;
		}

		if (qhn->qh->td_cur == last_tdn->ptd &&
			qhn->qh->td_overlay.next == TDLP_INVALID) {
			qhn->qh->td_overlay.next = tdn->ptd;
		}

		/* Add new TD to the queue and update the termination bit */
		last_tdn->next = tdn;
		last_tdn->td->next = tdn->ptd & ~TDLP_INVALID;

		usb_sync_unlock(edev->sync, qhn->lock);
	}

	/* Enable all TDs */
	while (tdn) {
		tdn->td->token &= ~TDTOK_SHALTED;
		tdn->td->token |= TDTOK_SACTIVE;
		tdn = tdn->next;
	}
}

void qhn_destroy(ps_dma_man_t* dman, struct QHn* qhn)
{
	struct TDn *tdn, *tmp;

	tdn = qhn->tdns;
	while (tdn) {
		tmp = tdn;
		tdn = tdn->next;
		if (tmp->cb) {
			tmp->cb(tmp->token, XACTSTAT_CANCELLED, 0);
		}
		ps_dma_free_pinned(dman, (void*)tmp->td, sizeof(struct TD));
		free(tmp);
	}

	ps_dma_free_pinned(dman, (void*)qhn->qh, sizeof(struct QH));
	free(qhn);
}

void ehci_async_complete(struct ehci_host *edev)
{
	struct QHn *qhn;
	struct TDn *tdn, *head, *tmp;
	int sum;

	qhn = edev->alist_tail;

	/* Nothing to do if the queue is empty */
	if (!qhn) {
		return;
	}

	do {
		usb_sync_lock(edev->sync, qhn->lock);

		tdn = qhn->tdns;
		sum = 0;
		head = tdn;
		while (tdn != NULL &&
			qtd_get_status(tdn->td) == XACTSTAT_SUCCESS) {
			sum += TDTOK_GET_BYTES(tdn->td->token);
			if (tdn->td->token & TDTOK_IOC) {
				if (tdn->cb) {
					tdn->cb(tdn->token, XACTSTAT_SUCCESS, sum);
					sum = 0;
				}

				qhn->tdns = tdn->next;

				/*
				 * Update the QH if we are about to dequeue the
				 * "previous" last TD in the queue. This happens
				 * when the last TD gets partially processed
				 * while we enqueue new TDs.
				 */
				if (qhn->tdns &&
					qhn->qh->td_cur == tdn->ptd &&
					qhn->qh->td_overlay.next == TDLP_INVALID) {
					qhn->qh->td_overlay.next = tdn->next->ptd;
				}

				/* Free */
				while (head != tdn->next) {
					tmp = head;
					head = head->next;
					ps_dma_free_pinned(edev->dman,
							(void*)tmp->td,
							sizeof(struct TD));
					free(tmp);
				}
			}
			tdn = tdn->next;
		}

		usb_sync_unlock(edev->sync, qhn->lock);
		qhn = qhn->next;
	} while (qhn != edev->alist_tail);
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

void ehci_del_qhn_async(struct ehci_host *edev, struct QHn *qhn)
{
	struct TDn *tdn;
	struct QHn *prev;

	/*
	 * The EHCI spec(section 4.8.2) gives the instructions of removing a QH
	 * from the active async queue. However, the instructions have been
	 * proved racy and impractical. See the following link for more
	 * information,
	 *
	 * https://marc.info/?l=linux-usb&m=144554811526940&w=2
	 *
	 * This implementation combines part of the spec instructions and
	 * Linux's double IAA cycle approach. Hope it would minimize the chances
	 * of trigging serious problems. Good Luck!
	 */

	/*
	 * Deactivate all active TDs
	 *
	 * This is the racy part of the EHCI spec. The host may have already in
	 * the middle of processing the TD overlay when we change the token.
	 * But we've got no choice otherwise the host would follow the TD list
	 * and would cause further errors.
	 */
	tdn = qhn->tdns;
	while (tdn) {
		if (tdn->td->token & TDTOK_SACTIVE) {
			tdn->td->token &= ~TDTOK_SACTIVE;
			tdn->td->token |= TDTOK_SHALTED;
		}
		tdn = tdn->next;
	}

	/* Select another queue head to set its H-bit */
	if ((qhn->qh->epc[0] & QHEPC0_H) && qhn->next) {
		qhn->next->qh->epc[0] |= QHEPC0_H;
	}

	/* Remove the queue head from async list */
	prev = edev->alist_tail;
	while (prev->next != qhn) {
		prev = prev->next;
	}

	prev->qh->qhlptr = qhn->qh->qhlptr;
	prev->next = qhn->next;

	if (edev->alist_tail == qhn) {
		edev->alist_tail = qhn->next;
	}

	/* Put the queue head to the recycle queue */
	if (edev->db_pending) {
		prev = edev->db_pending;
		while (prev->next != NULL) {
			prev = prev->next;
		}
		prev->next = qhn;
	} else {
		edev->db_pending = qhn;
	}
	qhn->next = NULL;
	qhn->was_cancelled = 1;

	/* Ring the doorbell */
	edev->op_regs->usbcmd |= EHCICMD_ASYNC_DB;
}

int ehci_wait_for_completion(struct TDn *tdn)
{
	int status;
	int cnt, sum = 0;

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
				return -1;
			}
			msdelay(1);
			cnt--;
		} while (status);
		tdn = tdn->next;
	}

	return sum;
}

/* TODO: Is it okay to use alist_tail and remove qhn */
void ehci_schedule_async(struct ehci_host* edev, struct QHn* qhn)
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
}

void check_doorbell(struct ehci_host* edev)
{
	int again = 0;
	struct QHn *qhn, *tmp, *tail;

	qhn = edev->db_pending;
	edev->db_pending = NULL;
	while (qhn) {
		tmp = qhn;
		qhn = qhn->next;

		/* Two IAA cycles have passed, safe to remove */
		if (tmp->was_cancelled > 1) {
			qhn_destroy(edev->dman, tmp);
		} else {
			tmp->was_cancelled++;
			if (!edev->db_pending) {
				edev->db_pending = tmp;
			} else {
				tail->next = tmp;
			}
			tail = tmp;

			again = 1;
		}
	}

	if (again) {
		edev->op_regs->usbcmd |= EHCICMD_ASYNC_DB;
	}
}

