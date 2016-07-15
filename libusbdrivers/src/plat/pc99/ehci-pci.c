/*
 * Copyright 2014, NICTA
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(NICTA_BSD)
 */
#include <platsupport/io.h>
#include <pci/pci.h>

#include <usb/usb_host.h>
#include "../../ehci/ehci.h"
#include "../../services.h"

#define USBLEGSUP            0x0
#define USBLEGSUP_OS         BIT(24)
#define USBLEGSUP_BIOS       BIT(16)
#define USBLEGSUP_NEXT_SHF   BIT(8)
#define USBLEGSUP_NEXT_MASK  0xFF 
#define USBLEGSUP_ID_SHF     BIT(0)
#define USBLEGSUP_ID_MASK    0xFF

#define USBLEGCTLSTS               0x4
#define USBLEGCTLSTS_BAR           BIT(31)
#define USBLEGCTLSTS_PCICMD        BIT(30)
#define USBLEGCTLSTS_OSOC          BIT(29)
#define USBLEGCTLSTS_AA            BIT(21)
#define USBLEGCTLSTS_HSE           BIT(20)
#define USBLEGCTLSTS_FLR           BIT(19)
#define USBLEGCTLSTS_PCD           BIT(18)
#define USBLEGCTLSTS_ERR           BIT(17)
#define USBLEGCTLSTS_COMP          BIT(16)
#define USBLEGCTLSTS_BAR_EN        BIT(15)
#define USBLEGCTLSTS_PCICMD_EN     BIT(14)
#define USBLEGCTLSTS_OSOC_EN       BIT(13)
#define USBLEGCTLSTS_AA_EN         BIT(5)
#define USBLEGCTLSTS_HSE_EN        BIT(4)
#define USBLEGCTLSTS_FLR_EN        BIT(3)
#define USBLEGCTLSTS_PC_EN         BIT(2)
#define USBLEGCTLSTS_ERR_EN        BIT(1)
#define USBLEGCTLSTS_SIM_EN        BIT(0)

/* Host vendor ID and device ID */
#define USB0_HOST_EHCI_VID    0x8086
#define USB0_HOST_EHCI_DID    0x1E2D
#define USB1_HOST_EHCI_VID    0x8086
#define USB1_HOST_EHCI_DID    0x1E26

static uintptr_t ehci_pci_init(uint16_t vid, uint16_t did,
		ps_io_ops_t *io_ops)
{
	int err;
	libpci_device_t *dev;
	volatile struct ehci_host_cap *cap_regs;
	uint32_t val;
	uint8_t reg;

	/* Find the device */
	libpci_scan(io_ops->io_port_ops);
	dev = libpci_find_device(vid, did);
	if (dev) {
		libpci_read_ioconfig(&dev->cfg, dev->bus, dev->dev, dev->fun);
		/* Map device memory */
		cap_regs = (volatile struct echi_host_cap*)MAP_DEVICE(io_ops,
				dev->cfg.base_addr[0],
				dev->cfg.base_addr_size[0]);
		assert(cap_regs);
	} else {
		printf("EHCI: Host device not found!\n");
		assert(0);
	}

	/* Check EHCI Extend Capabilities Pointer(Section 2.2.4) */
	reg = EHCI_HCC_EECP(cap_regs->hccparams);
	if (reg) {
		reg += USBLEGSUP;
		/* Take the ownership from BIOS */
		val = libpci_read_reg32(dev->bus, dev->dev, dev->fun, reg);
		val |= USBLEGSUP_OS;
		libpci_write_reg32(dev->bus, dev->dev, dev->fun, reg, val);
		do {
			val = libpci_read_reg32(dev->bus, dev->dev,
					dev->fun, reg);
		} while (val & USBLEGSUP_BIOS);

		if ((val >> USBLEGSUP_NEXT_SHF) & USBLEGSUP_NEXT_MASK) {
			printf("EHCI: Warning! More Capability Registers.\n");
		}
	}

	return (uintptr_t)cap_regs;
}

int
usb_host_init(enum usb_host_id id, ps_io_ops_t* io_ops, usb_host_t* hdev)
{
	int err;
	uint16_t vid, did;
	uintptr_t usb_regs;

	if (id < 0 || id > USB_NHOSTS) {
		return -1;
	}
	assert(io_ops);
	assert(hdev);

	hdev->id = id;
	hdev->dman = &io_ops->dma_manager;

	switch (id) {
		case 0:
			vid = USB0_HOST_EHCI_VID;
			did = USB0_HOST_EHCI_DID;
			break;
		case 1:
			vid = USB1_HOST_EHCI_VID;
			did = USB1_HOST_EHCI_DID;
			break;
		default:
			assert(0);
			break;
	}

	/* Check device mappings */
	usb_regs = ehci_pci_init(vid, did, io_ops);
	if (!usb_regs) {
		return -1;
	}

	err = ehci_host_init(hdev, usb_regs, NULL);

	return err;
}

