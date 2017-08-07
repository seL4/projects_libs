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
#include "../services.h"
#include "ehci.h"

/***************************
 *** Hub emulation stubs ***
 ***************************/
volatile uint32_t *_get_portsc(struct ehci_host *h, int port)
{
	volatile uint32_t *reg;

	if (port <= 0 || port > EHCI_HCS_N_PORTS(h->cap_regs->hcsparams)) {
		ZF_LOGF("Invalid port\n");
	}
	reg = &h->op_regs->portsc[port - 1];
	return reg;
}

int _set_pf(void *token, int port, enum port_feature feature)
{
	struct ehci_host *edev = (struct ehci_host *)token;
	volatile uint32_t *ps_reg = _get_portsc(edev, port);
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
		if ((edev->op_regs->usbsts & EHCISTS_HCHALTED) != 0) {
			ZF_LOGF("Failed to rest the host port\n");
		}
		edev->bmreset_c = BIT(port);
		v &= ~EHCI_PORT_ENABLE;
		v |= EHCI_PORT_RESET;
		/* Perform the reset */
		*ps_reg = v;
		/* Sabre will automatically stop the reset and a ENABLE CHANGE
		 * IRQ event fires, but this does not happen on the Odroid! */
		/* Wait for reset to complete */
		msdelay(10);	/* 7.1.7.5 of USB 0.2 10ms delay */
		*ps_reg &= ~EHCI_PORT_RESET;
		while (*ps_reg & EHCI_PORT_RESET) ;

		return 0;
	case PORT_SUSPEND:
		/* Must port owner 0 */
		if (*ps_reg & EHCI_PORT_OWNER) {
			ZF_LOGF("Failed to suspend the port\n");
		}
		/* Perform the Suspend */
		v |= EHCI_PORT_SUSPEND;
		break;
	default:
		ZF_LOGD("EHCI: Unknown feature %d for set feature request\n",
		       feature);
		return -1;
	}
	*ps_reg = v;
	return 0;
}

int _clr_pf(void *token, int port, enum port_feature feature)
{
	struct ehci_host *edev = (struct ehci_host *)token;
	volatile uint32_t *ps_reg = _get_portsc(edev, port);
	/* Change bits are write-1-to-clear so need to mask then */
	uint32_t v = *ps_reg & ~(EHCI_PORT_CHANGE);
	switch (feature) {
	case PORT_ENABLE:
		v &= ~EHCI_PORT_ENABLE;
		break;
	case PORT_OVER_CURRENT:
		v &= ~EHCI_PORT_OCURRENT;
		break;
	case C_PORT_ENABLE:
		v |= EHCI_PORT_ENABLE_C;
		break;
	case C_PORT_CONNECTION:
		v |= EHCI_PORT_CONNECT_C;
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
		if (!(v & EHCI_PORT_ENABLE)) {
			ZF_LOGF("Port must be enabled\n");
		}
		/* Must be in suspend state */
		if (!(v & EHCI_PORT_SUSPEND)) {
			ZF_LOGF("Port must be suspend\n");
		}
		/* Perform the Suspend */
		v |= EHCI_PORT_FORCE_RESUME;
		break;

	default:
		ZF_LOGD("EHCI: Unknown feature %d for clear feature request\n",
		       feature);
		return -1;
	}
	udelay(10);
	*ps_reg = v;
	udelay(10);
	return 0;
}

int _get_pstat(void *token, int port, struct port_status *_ps)
{
	struct ehci_host *edev = (struct ehci_host *)token;
	uint32_t v;
	struct port_status ps;
	v = *_get_portsc(edev, port);
	/* Hey EHCI, here's an idea: Why not pull your spec inline with the USB hub spec? */
	ps.wPortStatus =
	    ((v & EHCI_PORT_CONNECT) ? BIT(PORT_CONNECTION) : 0) |
	    ((v & EHCI_PORT_ENABLE) ? BIT(PORT_ENABLE) : 0) |
	    ((v & EHCI_PORT_SUSPEND) ? BIT(PORT_SUSPEND) : 0) |
	    ((v & EHCI_PORT_OCURRENT) ? BIT(PORT_OVER_CURRENT) : 0) |
	    ((v & EHCI_PORT_RESET) ? BIT(PORT_RESET) : 0) |
	    ((v & EHCI_PORT_POWER) ? BIT(PORT_POWER) : 0) | 0;
	ps.wPortChange =
	    ((v & EHCI_PORT_CONNECT_C) ? BIT(PORT_CONNECTION) : 0) |
	    ((v & EHCI_PORT_ENABLE_C) ? BIT(PORT_ENABLE) : 0) |
	    ((v & EHCI_PORT_OCURRENT_C) ? BIT(PORT_OVER_CURRENT) : 0) | 0;
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
