/*
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */
#include "../services.h"
#include "ehci.h"

/**************************
 **** Queue scheduling ****
 **************************/
/*
 * TODO: We only support interrupt endpoint at the moment, this function is
 * subject to change when we add isochronous endpoint support.
 */
void ehci_add_qhn_periodic(struct ehci_host *edev, struct QHn *qhn)
{
	struct QHn *cur;

	/* Allocate the frame list */
	if (!edev->flist) {
		/* XXX: The frame list size is default to 1024 */
		edev->flist_size = 1024;
		edev->flist = ps_dma_alloc_pinned(edev->dman,
				edev->flist_size * sizeof(uint32_t*), 0x1000, 0,
				PS_MEM_NORMAL, &edev->pflist);
		if (!edev->flist) {
			ZF_LOGF("Out of DMA memory\n");
		}

		/* Mark all frames as disabled */
		for (int i = 0; i < edev->flist_size; i++) {
			edev->flist[i] = TDLP_INVALID;
		}

		/*
		 * Allocate shadow frame list to keep track of the virtual
		 * address of the queue heads.
		 */
		edev->shadow_flist = (struct QHn**)usb_malloc(
				sizeof(struct QHn*) * edev->flist_size);
		if (!edev->shadow_flist) {
			ZF_LOGF("Out of memory\n");
		}
	}

	/* Check if the queue head has already been scheduled */
	cur = edev->intn_list;
	while (cur) {
		if (cur == qhn) {
			return;
		}
		cur = cur->next;
	}

	/*
	 * Insert the queue head into the list. The queue heads are sorted, from
	 * low rate to high rate.
	 */
	cur = edev->intn_list;
	qhn->qh->qhlptr = QHLP_INVALID;
	if (!cur) {
		edev->intn_list = qhn;
		qhn->next = NULL;
	} else {
		while (cur->next && cur->next->rate >= qhn->rate) {
			cur = cur->next;
		}
		qhn->next = cur->next;
		cur->next = qhn;

		cur->qh->qhlptr = qhn->pqh | QHLP_TYPE_QH;
		if (qhn->next) {
			qhn->qh->qhlptr = qhn->next->pqh | QHLP_TYPE_QH;
		}
	}

	/* Update the frame list */
	for (int i = qhn->rate - 1; i < edev->flist_size; i += qhn->rate) {
		cur = edev->shadow_flist[i];
		if (!cur || cur->rate < qhn->rate) {
			edev->shadow_flist[i] = qhn;
			edev->flist[i] = qhn->pqh | QHLP_TYPE_QH;
		}
	}

	dsb();
}

/*
 * FIXME: If the queue head is for a full/low speed device. Simply removing it
 * could cause problems with ongoing split transaction. We cannot wait until the
 * current TD returns, because the interrupt could never happen, i.e the TD
 * remains active. The correct way is to use the "Inactive on Next Transaction"
 * bit in the queue head. Read EHCI spec 4.12.2.5.
 */
void ehci_del_qhn_periodic(struct ehci_host *edev, struct QHn *qhn)
{
	struct QHn *cur;
	struct TDn *tdn;

	/* Remove the active bit from the TD */
	tdn = qhn->tdns;
	tdn->td->token &= ~TDTOK_SACTIVE;

	/* Remove from the software list */
	cur = edev->intn_list;
	if (cur == qhn) {
		edev->intn_list = qhn->next;
	} else {
		while (cur->next && cur->next != qhn) {
			cur = cur->next;
		}
		cur->next = qhn->next;
		if (qhn->next) {
			cur->qh->qhlptr = qhn->next->pqh | QHLP_TYPE_QH;
		} else {
			cur->qh->qhlptr = QHLP_INVALID;
		}
	}

	/* Remove from the periodic schedule table */
	for (int i = qhn->rate - 1; i < edev->flist_size; i += qhn->rate) {
		cur = edev->shadow_flist[i];

		/*
		 * If we are removing the first element, we need to update the
		 * hardware frame list. There's no need to check if removing
		 * queue head points to another valid queue head, because even
		 * if this queue head points to NULL, its horizontal link pointer
		 * should have already had the terminate bit set to 1.
		 */
		if (cur == qhn) {
			edev->shadow_flist[i] = qhn->next;
			edev->flist[i] = qhn->qh->qhlptr;
		}
	}

	dsb();

	/* Free */
	ps_dma_free_pinned(edev->dman, qhn->tdns, sizeof(struct TDn));
	usb_free(qhn->tdns);
	qhn->tdns = NULL;

	ps_dma_free_pinned(edev->dman, qhn, sizeof(struct QHn));
	usb_free(qhn);
}

int ehci_schedule_periodic_root(struct ehci_host *edev, struct xact *xact,
                            int nxact, usb_cb_t cb, void *t)
{
	int port;

	if (!xact->vaddr || !cb) {
		ZF_LOGF("Invalid arguments\n");
	}
	edev->irq_xact = *xact;
	edev->irq_cb = cb;
	edev->irq_token = t;
	/* Enable IRQS */
	for (port = 1; port <= EHCI_HCS_N_PORTS(edev->cap_regs->hcsparams); port++) {
		volatile uint32_t *ps_reg = _get_portsc(edev, port);
		uint32_t v;
		v = *ps_reg & ~(EHCI_PORT_CHANGE);
		v |= (EHCI_PORT_WO_OCURRENT | EHCI_PORT_WO_DCONNECT |
			EHCI_PORT_WO_CONNECT);
		*ps_reg = v;
	}
	edev->op_regs->usbintr |= EHCIINTR_PORTC_DET;
	return 0;
}

int ehci_schedule_periodic(struct ehci_host *edev)
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

void ehci_periodic_complete(struct ehci_host *edev)
{
	struct QHn *qhn;
	struct TDn *tdn;
	int sum;

	/* Interrupt endpoints would never queue multiple TDs */
	qhn = edev->intn_list;
	while (qhn) {
		tdn = qhn->tdns;
		if (tdn && qtd_get_status(tdn->td) == XACTSTAT_SUCCESS) {
			/* Restore the queue head to its initial state */
			qhn->tdns = NULL;

			sum = TDTOK_GET_BYTES(tdn->td->token);
			if (tdn->cb) {
				tdn->cb(tdn->token, XACTSTAT_SUCCESS, sum);
			}

			ps_dma_free_pinned(edev->dman, (void*)tdn->td,
					sizeof(struct TD));
			usb_free(tdn);
		}
		qhn = qhn->next;
	}
}

