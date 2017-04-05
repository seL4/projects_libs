/*
 * Copyright 2016, NICTA
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(NICTA_BSD)
 */

#include <math.h>

#include "../services.h"
#include "ehci.h"

struct usb_hc_data {
    struct ehci_host edev;
};

/*****************
 **** Helpers ****
 *****************/

static inline struct ehci_host*
_hcd_to_ehci(usb_host_t* hcd)
{
    struct usb_hc_data* hc_data = (struct usb_hc_data*)hcd->pdata;
    assert(hc_data);
    return &hc_data->edev;
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
    if (resched) {
        usb_assert(0);
    }
}

void ehci_sched_enable_irq(struct ehci_host *edev)
{
	uint32_t irq;

	irq = edev->op_regs->usbintr;
	irq |= EHCIINTR_USBINT;
	edev->op_regs->usbintr = irq;
}

void ehci_sched_disable_irq(struct ehci_host *edev)
{
	uint32_t irq;

	irq = edev->op_regs->usbintr;
	irq &= ~EHCIINTR_USBINT;
	edev->op_regs->usbintr = irq;
}

int ehci_schedule_xact(usb_host_t* hdev, uint8_t addr, int8_t hub_addr, uint8_t hub_port,
                   enum usb_speed speed, struct endpoint *ep, struct xact* xact,
		   int nxact, usb_cb_t cb, void* t)
{
    struct QHn *qhn;
    struct TDn *tdn;
    struct ehci_host* edev;
    int ret;

    usb_assert(hdev);
    edev = _hcd_to_ehci(hdev);
    if (hub_addr == -1) {
        /* Send off to root handler... No need to create QHn */
        if (ep->type == EP_INTERRUPT) {
            return ehci_schedule_periodic_root(edev, xact, nxact, cb, t);
        } else {
            return hubem_process_xact(edev->hubem, xact, nxact, cb, t);
        }
    }

    qhn = (struct QHn*)ep->hcpriv;
    if (!qhn) {
	    qhn = qhn_alloc(edev, addr, hub_addr, hub_port, speed, ep);
	    qhn->lock = usb_sync_init(edev->sync, 1);
	    ep->hcpriv = qhn;

	    if (ep->type == EP_CONTROL || ep->type == EP_BULK) {
		    ehci_add_qhn_async(edev, qhn);
	    } else {
		    /*
		     * Polling rate calculation.
		     * USB spec 5.6.4, 5.7.4 and 9.6.6
		     *
		     * In order to maintain the integrity of periodic schedule,
		     * all polling rates have to be a power of two.
		     */
		    if (speed == USBSPEED_HIGH) {
			    qhn->rate = (1 << (ep->interval - 1)) / 8;
		    } else if (speed == USBSPEED_FULL && ep->type == EP_ISOCHRONOUS) {
			    qhn->rate = (1 << (ep->interval - 1));
		    } else {
			    qhn->rate = (1 << ilogb(ep->interval));
		    }

		    /* Make sure the rate is sane */
		    if (qhn->rate < 1) {
			    qhn->rate = 1;
		    } if (qhn->rate > 1024) {
			    qhn->rate = 1024;
		    }

		    ehci_add_qhn_periodic(edev, qhn);
	    }

    } else {
	    /*
	     * The address and maximum packet size could change after initial
	     * enumeration, update the queue head accordingly.
	     */
	    qhn_update(qhn, addr, ep);
    }

    /* Allocate qTD */
    tdn = qtd_alloc(edev, speed, ep, xact, nxact, cb, t);

    /* Add qTD to the queue head and send off over the bus */
    if (ep->type == EP_BULK || ep->type == EP_CONTROL) {
        ehci_schedule_async(edev, qhn);
	if (cb) {
		qtd_enqueue(edev, qhn, tdn);
		return 0;
	} else {
		/* Wait for the existing TD to be processed */
		while(qhn->tdns != NULL);
		ehci_sched_disable_irq(edev);
		qtd_enqueue(edev, qhn, tdn);
		ret = ehci_wait_for_completion(tdn);
		ehci_async_complete(edev);
		ehci_sched_enable_irq(edev);
		return ret;
	}
    } else {
	qtd_enqueue(edev, qhn, tdn);
	return ehci_schedule_periodic(edev);
    }
}

void ehci_handle_irq(usb_host_t *hdev)
{
	struct ehci_host *edev = _hcd_to_ehci(hdev);
	uint32_t sts;

	sts = edev->op_regs->usbsts & EHCISTS_MASK;

	/* We cannot recover from fatal host error */
	if (sts & EHCISTS_HOST_ERR) {
		EHCI_IRQDBG(edev, "INT - host error\n");
		usb_assert(0);
	}

	if (sts & EHCISTS_USBINT) {
		EHCI_IRQDBG(edev, "INT - USB\n");
		ehci_periodic_complete(edev);
		ehci_async_complete(edev);
	}

	/*
	 * TODO: To recover from transaction error, we need to scan both the
	 * periodic queue and the async queue, find the error TD and reschedule
	 * it.
	 */
	if (sts & EHCISTS_USBERRINT) {
		EHCI_IRQDBG(edev, "INT - USB error\n");
	}

	/*
	 * We don't handle frame list roll over interrupt, but some controllers
	 * don't like it always being set to 1, so simply clear it.
	 */
	if (sts & EHCISTS_FLIST_ROLL) {
		EHCI_IRQDBG(edev, "INT - Frame list roll over\n");
	}

	if (sts & EHCISTS_PORTC_DET) {
		EHCI_IRQDBG(edev, "INT - root hub port change\n");
		_root_irq(edev);
	}

	if (sts & EHCISTS_ASYNC_ADV) {
		EHCI_IRQDBG(edev, "INT - async list advance\n");
		check_doorbell(edev);
	}

	/* Write to clear */
	edev->op_regs->usbsts = sts;
}

int ehci_cancel_xact(usb_host_t* hdev, struct endpoint *ep)
{
	struct ehci_host* edev = _hcd_to_ehci(hdev);

	usb_assert(ep);

	if (ep->hcpriv) {
		if (ep->type == EP_BULK || ep->type == EP_CONTROL) {
			ehci_del_qhn_async(edev, ep->hcpriv);
		} else {
			ehci_del_qhn_periodic(edev, ep->hcpriv);
		}
	}

	return 0;
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
    hdev->pdata = (struct usb_hc_data*)usb_malloc(sizeof(struct usb_hc_data));
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

    /* If the host controller has 64-bit capability, it is compulsory to use
     * 64-bit data structure(Section 2.2.4). Clear the most significant address
     * for EHCI data structures, since we are running a 32-bit OS */
    if (edev->cap_regs->hccparams & EHCI_HCC_64BIT) {
        edev->op_regs->ctrldssegment = 0;
    }

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
    edev->sync = hdev->sync;

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

