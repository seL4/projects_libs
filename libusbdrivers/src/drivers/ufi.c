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

/**
 * @brief UFI Command implementation for USB Mass Storage Class
 * @see USB Mass Storage Class UFI Command Spec
 */
#include <platsupport/delay.h>
#include <string.h>
#include <usb/drivers/ufi.h>

#include "storage.h"

/* Operation Code */
#define TEST_UNIT_READY    0x00
#define REZERO_UNIT        0x01
#define REQUEST_SENSE      0x03
#define FORMAT_UNIT        0x04

#define INQUIRY            0x12
#define START_STOP         0x1B
#define SEND_DIAGNOSTIC    0x1D
#define ALLOW_REMOVAL      0x1E

#define READ_FORMAT_CAP    0x23
#define READ_CAPACITY      0x25
#define READ_10            0x28
#define WRITE_10           0x2A
#define SEEK               0x2B
#define WRITE_VERIFY       0x2E
#define VERIFY             0x2F

#define MODE_SELECT        0x55
#define MODE_SENSE         0x5A
#define READ_12            0xA8
#define WRITE_12           0xAA


#define UFI_OUTPUT  0
#define UFI_INPUT   1

#define UFI_LBA_SHF   16
#define UFI_SENSE_LEN 18
#define UFI_INQ_LEN   36
#define UFI_MODE_SENSE_LEN 192
#define UFI_MODE_PAGE_ALL 0x3F
#define UFI_BLK_SIZE  512

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
}

static void ufi_test_unit_ready(usb_dev_t *udev)
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
	}
}

static void ufi_request_sense(usb_dev_t *udev)
{
	int err;
	struct ufi_cdb cdb;
	struct xact data;

	memset(&cdb, 0, sizeof(struct ufi_cdb));

	/* Fill in the command */
	cdb.opcode = REQUEST_SENSE;
	cdb.lba = UFI_SENSE_LEN << UFI_LBA_SHF;

	data.type = PID_IN;
	data.len = UFI_SENSE_LEN;
	err = usb_alloc_xact(udev->dman, &data, 1);
	if (err) {
		ZF_LOGF("Out of DMA memory\n");
	}
	err = usb_storage_xfer(udev, &cdb, sizeof(struct ufi_cdb),
			&data, 1, UFI_INPUT);
	if (err) {
		ZF_LOGF("Transfer error\n");
	}
	usb_destroy_xact(udev->dman, &data, 1);
}

static void ufi_inquiry(usb_dev_t *udev)
{
	int err;
	struct ufi_cdb cdb;
	struct xact data;

	memset(&cdb, 0, sizeof(struct ufi_cdb));

	/* Inquiry UFI disk */
	cdb.opcode = INQUIRY;
	cdb.lba = UFI_INQ_LEN << UFI_LBA_SHF;

	data.type = PID_IN;
	data.len = UFI_INQ_LEN;
	err = usb_alloc_xact(udev->dman, &data, 1);
	if (err) {
		ZF_LOGF("Out of DMA memory\n");
	}

	err = usb_storage_xfer(udev, &cdb, sizeof(struct ufi_cdb),
			&data, 1, UFI_INPUT);
	if (err) {
		ZF_LOGF("Transfer error\n");
	}

	usb_destroy_xact(udev->dman, &data, 1);
}

static void ufi_prevent_allow_medium_removal(usb_dev_t *udev, int enable)
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
	}
}

uint32_t ufi_read_capacity(usb_dev_t *udev)
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
	}

	err = usb_storage_xfer(udev, &cdb, sizeof(struct ufi_cdb),
				&data, 1, UFI_INPUT);
	if (err) {
		ZF_LOGF("Transfer error\n");
	}

	ret = *(uint32_t*)data.vaddr;
	usb_destroy_xact(udev->dman, &data, 1);

	return ret;
}

static void ufi_mode_sense(usb_dev_t *udev)
{
	int err;
	struct ufi_cdb cdb;
	struct xact data;

	memset(&cdb, 0, sizeof(struct ufi_cdb));

	cdb.opcode = MODE_SENSE;
	cdb.lba = UFI_MODE_PAGE_ALL;
	cdb.length = UFI_MODE_SENSE_LEN << UFI_LBA_SHF;

	data.type = PID_IN;
	data.len = UFI_MODE_SENSE_LEN;
	err = usb_alloc_xact(udev->dman, &data, 1);
	if (err) {
		ZF_LOGF("Out of DMA memory\n");
	}

	err = usb_storage_xfer(udev, &cdb, sizeof(struct ufi_cdb),
				&data, 1, UFI_INPUT);
	if (err) {
		ZF_LOGF("Transfer error\n");
	}

	usb_destroy_xact(udev->dman, &data, 1);
}

static void ufi_read10(usb_dev_t *udev, uint32_t lba, uint16_t count)
{
	int err;
	struct ufi_cdb cdb;
	struct xact data;

	memset(&cdb, 0, sizeof(struct ufi_cdb));

	cdb.opcode = READ_10;
	cdb.lba = __builtin_bswap32(lba);
	cdb.length = __builtin_bswap16(count) << 8;

	data.type = PID_IN;
	data.len = UFI_BLK_SIZE * count;

	err = usb_alloc_xact(udev->dman, &data, 1);
	if (err) {
		ZF_LOGF("Out of DMA memory\n");
	}

	err = usb_storage_xfer(udev, &cdb, sizeof(struct ufi_cdb),
				&data, 1, UFI_INPUT);
	if (err) {
		ZF_LOGF("Transfer error\n");
	}

	usb_destroy_xact(udev->dman, &data, 1);
}

static void ufi_read12(usb_dev_t *udev, uint32_t lba, uint32_t count)
{
	int err;
	struct ufi_cdb cdb;
	struct xact data;

	memset(&cdb, 0, sizeof(struct ufi_cdb));

	cdb.opcode = READ_12;
	cdb.lba = __builtin_bswap32(lba);
	cdb.length = __builtin_bswap16(count);

	data.type = PID_IN;
	data.len = UFI_BLK_SIZE * count;

	err = usb_alloc_xact(udev->dman, &data, 1);
	if (err) {
		ZF_LOGF("Out of DMA memory\n");
	}

	err = usb_storage_xfer(udev, &cdb, sizeof(struct ufi_cdb),
				&data, 1, UFI_INPUT);
	if (err) {
		ZF_LOGF("Transfer error\n");
	}

	usb_destroy_xact(udev->dman, &data, 1);
}

int
ufi_init_disk(usb_dev_t *udev)
{
	usb_storage_bind(udev);

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

