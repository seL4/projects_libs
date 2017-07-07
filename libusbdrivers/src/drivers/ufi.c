/*
 * Copyright 2015, NICTA
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(NICTA_BSD)
 */

#include <platsupport/delay.h>
#include <string.h>
#include "storage.h"
#include "ufi.h"

#define UFI_OUTPUT  0
#define UFI_INPUT   1

/* Command Descriptor Block */
struct ufi_cdb {
	uint8_t opcode;
	uint8_t lun;
	uint32_t lba;
	uint32_t length;
	uint16_t reserved;
} __attribute__((packed));

static void ufi_format_unit()
{
	ZF_LOGF("Not implemented\n");
	abort();
}

static void ufi_test_unit_ready(usb_dev_t udev)
{
	int err;
	struct ufi_cdb cdb;

	memset(&cdb, 0, sizeof(struct ufi_cdb));

	/* Fill in the command */
	cdb.opcode = TEST_UNIT_READY;

	err = usb_storage_xfer(udev, &cdb, sizeof(struct ufi_cdb),
			NULL, 0, UFI_OUTPUT);
	if (err) {
		ZF_LOGF("Transfer error\n");
		abort();
	}
}

static void ufi_request_sense(usb_dev_t udev)
{
	int err;
	struct ufi_cdb cdb;
	struct xact data;

	memset(&cdb, 0, sizeof(struct ufi_cdb));

	/* Fill in the command */
	cdb.opcode = REQUEST_SENSE;
	cdb.lba = 18 << 16;

	data.type = PID_IN;
	data.len = 18;
	err = usb_alloc_xact(udev->dman, &data, 1);
	if (err) {
		ZF_LOGF("Out of DMA memory\n");
		abort();
	}
	err = usb_storage_xfer(udev, &cdb, sizeof(struct ufi_cdb),
			&data, 1, UFI_INPUT);
	if (err) {
		ZF_LOGF("Transfer error\n");
		abort();
	}
	usb_destroy_xact(udev->dman, &data, 1);
}

static void ufi_inquiry(usb_dev_t udev)
{
	int err;
	struct ufi_cdb cdb;
	struct xact data;

	memset(&cdb, 0, sizeof(struct ufi_cdb));

	/* Inquiry UFI disk */
	cdb.opcode = INQUIRY;
	cdb.lba = 36 << 16;

	data.type = PID_IN;
	data.len = 36;
	err = usb_alloc_xact(udev->dman, &data, 1);
	if (err) {
		ZF_LOGF("Out of DMA memory\n");
		abort();
	}

	err = usb_storage_xfer(udev, &cdb, sizeof(struct ufi_cdb),
			&data, 1, UFI_INPUT);
	if (err) {
		ZF_LOGF("Transfer error\n");
		abort();
	}

	usb_destroy_xact(udev->dman, &data, 1);
}

static void ufi_prevent_allow_medium_removal(usb_dev_t udev, int enable)
{
	int err;
	struct ufi_cdb cdb;

	memset(&cdb, 0, sizeof(struct ufi_cdb));

	cdb.opcode = ALLOW_REMOVAL;
	cdb.lba = enable << 8;

	err = usb_storage_xfer(udev, &cdb, sizeof(struct ufi_cdb),
			NULL, 0, UFI_OUTPUT);
	if (err) {
		ZF_LOGF("Transfer error\n");
		abort();
	}
}

uint32_t ufi_read_capacity(usb_dev_t udev)
{
	int err;
	uint32_t ret;
	struct ufi_cdb cdb;
	struct xact data;

	memset(&cdb, 0, sizeof(struct ufi_cdb));

	cdb.opcode = READ_CAPACITY;

	data.type = PID_IN;
	data.len = 8;
	err = usb_alloc_xact(udev->dman, &data, 1);
	if (err) {
		ZF_LOGF("Out of DMA memory\n");
		abort();
	}

	err = usb_storage_xfer(udev, &cdb, sizeof(struct ufi_cdb),
				&data, 1, UFI_INPUT);
	if (err) {
		ZF_LOGF("Transfer error\n");
		abort();
	}

	ret = *(uint32_t*)data.vaddr;
	usb_destroy_xact(udev->dman, &data, 1);

	return ret;
}

static void ufi_mode_sense(usb_dev_t udev)
{
	int err;
	struct ufi_cdb cdb;
	struct xact data;

	memset(&cdb, 0, sizeof(struct ufi_cdb));

	cdb.opcode = MODE_SENSE;
	cdb.lba = 0x3F;
	cdb.length = 192 << 16;

	data.type = PID_IN;
	data.len = 192;
	err = usb_alloc_xact(udev->dman, &data, 1);
	if (err) {
		ZF_LOGF("Out of DMA memory\n");
		abort();
	}

	err = usb_storage_xfer(udev, &cdb, sizeof(struct ufi_cdb),
				&data, 1, UFI_INPUT);
	if (err) {
		ZF_LOGF("Transfer error\n");
		abort();
	}

	usb_destroy_xact(udev->dman, &data, 1);
}

static void ufi_read10(usb_dev_t udev, uint32_t lba, uint16_t count)
{
	int err;
	struct ufi_cdb cdb;
	struct xact data;

	memset(&cdb, 0, sizeof(struct ufi_cdb));

	cdb.opcode = READ_10;
	cdb.lba = __builtin_bswap32(lba);
	cdb.length = __builtin_bswap16(count) << 8;

	data.type = PID_IN;
	data.len = 512 * count;

	err = usb_alloc_xact(udev->dman, &data, 1);
	if (err) {
		ZF_LOGF("Out of DMA memory\n");
		abort();
	}

	err = usb_storage_xfer(udev, &cdb, sizeof(struct ufi_cdb),
				&data, 1, UFI_INPUT);
	if (err) {
		ZF_LOGF("Transfer error\n");
		abort();
	}

	usb_destroy_xact(udev->dman, &data, 1);
}

static void ufi_read12(usb_dev_t udev, uint32_t lba, uint32_t count)
{
	int err;
	struct ufi_cdb cdb;
	struct xact data;

	memset(&cdb, 0, sizeof(struct ufi_cdb));

	cdb.opcode = READ_12;
	cdb.lba = __builtin_bswap32(lba);
	cdb.length = __builtin_bswap16(count);

	data.type = PID_IN;
	data.len = 512 * count;

	err = usb_alloc_xact(udev->dman, &data, 1);
	if (err) {
		ZF_LOGF("Out of DMA memory\n");
		abort();
	}

	err = usb_storage_xfer(udev, &cdb, sizeof(struct ufi_cdb),
				&data, 1, UFI_INPUT);
	if (err) {
		ZF_LOGF("Transfer error\n");
		abort();
	}

	usb_destroy_xact(udev->dman, &data, 1);
}

int
ufi_init_disk(usb_dev_t udev)
{
	ufi_inquiry(udev);
	ufi_test_unit_ready(udev);

	ufi_request_sense(udev);
	ufi_test_unit_ready(udev);

	ufi_read_capacity(udev);
	ufi_mode_sense(udev);
	ufi_test_unit_ready(udev);

	ufi_prevent_allow_medium_removal(udev, 0);
	ufi_request_sense(udev);
	ufi_test_unit_ready(udev);

	return 0;
}

