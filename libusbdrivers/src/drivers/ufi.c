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

struct ufi_disk {
	usb_dev_t udev;
};

static void ufi_print_info(char *info)
{
	int i = 0;

	for (i = 0; i < 36; i++) {
		printf("%x, ", info[i]);
		if ((i+1) % 8 == 0) {
			printf("\n");
		}
	}
	printf("\n");
}

static void ufi_format_unit()
{
	assert(0);
}

static void ufi_test_unit_ready(struct ufi_disk *disk)
{
	int err;
	struct ufi_cdb cdb;
	struct xact data;

	memset(&cdb, 0, sizeof(struct ufi_cdb));

	/* Fill in the command */
	cdb.opcode = TEST_UNIT_READY;

	err = usb_storage_xfer(disk->udev, &cdb, sizeof(struct ufi_cdb),
			NULL, 0, UFI_OUTPUT);
	assert(!err);
}

static void ufi_request_sense(struct ufi_disk *disk)
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
	err = usb_alloc_xact(disk->udev->dman, &data, 1);
	assert(!err);
	err = usb_storage_xfer(disk->udev, &cdb, sizeof(struct ufi_cdb),
			&data, 1, UFI_INPUT);
	assert(!err);
	usb_destroy_xact(disk->udev->dman, &data, 1);
}

static void ufi_inquiry(struct ufi_disk *disk)
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
	err = usb_alloc_xact(disk->udev->dman, &data, 1);
	assert(!err);

	err = usb_storage_xfer(disk->udev, &cdb, sizeof(struct ufi_cdb),
			&data, 1, UFI_INPUT);
	assert(!err);

	usb_destroy_xact(disk->udev->dman, &data, 1);
}

static void ufi_prevent_allow_medium_removal(struct ufi_disk *disk, int enable)
{
	int err;
	struct ufi_cdb cdb;

	memset(&cdb, 0, sizeof(struct ufi_cdb));

	cdb.opcode = ALLOW_REMOVAL;
	cdb.lba = enable << 8;

	err = usb_storage_xfer(disk->udev, &cdb, sizeof(struct ufi_cdb),
			NULL, 0, UFI_OUTPUT);
	assert(!err);
}

static void ufi_read_capacity(struct ufi_disk *disk)
{
	int err;
	struct ufi_cdb cdb;
	struct xact data;

	memset(&cdb, 0, sizeof(struct ufi_cdb));

	cdb.opcode = READ_CAPACITY;

	data.type = PID_IN;
	data.len = 8;
	err = usb_alloc_xact(disk->udev->dman, &data, 1);
	assert(!err);

	err = usb_storage_xfer(disk->udev, &cdb, sizeof(struct ufi_cdb),
				&data, 1, UFI_INPUT);
	assert(!err);

	usb_destroy_xact(disk->udev->dman, &data, 1);
}

static void ufi_mode_sense(struct ufi_disk *disk)
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
	err = usb_alloc_xact(disk->udev->dman, &data, 1);
	assert(!err);

	err = usb_storage_xfer(disk->udev, &cdb, sizeof(struct ufi_cdb),
				&data, 1, UFI_INPUT);
	assert(!err);

	usb_destroy_xact(disk->udev->dman, &data, 1);
}

static void ufi_read10(struct ufi_disk *disk, uint32_t lba, uint16_t count)
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

	err = usb_alloc_xact(disk->udev->dman, &data, 1);
	assert(!err);

	err = usb_storage_xfer(disk->udev, &cdb, sizeof(struct ufi_cdb),
				&data, 1, UFI_INPUT);
	assert(!err);

	usb_destroy_xact(disk->udev->dman, &data, 1);
}

static void ufi_read12(struct ufi_disk *disk, uint32_t lba, uint32_t count)
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

	err = usb_alloc_xact(disk->udev->dman, &data, 1);
	assert(!err);

	err = usb_storage_xfer(disk->udev, &cdb, sizeof(struct ufi_cdb),
				&data, 1, UFI_INPUT);
	assert(!err);

	usb_destroy_xact(disk->udev->dman, &data, 1);
}

int
ufi_init_disk(usb_dev_t udev)
{
	struct ufi_disk *disk;

	disk = malloc(sizeof(struct ufi_disk));
	assert(disk);

	disk->udev = udev;

	ufi_inquiry(disk);
	ufi_test_unit_ready(disk);

	ufi_request_sense(disk);
	ufi_test_unit_ready(disk);

	ufi_read_capacity(disk);
	ufi_mode_sense(disk);
	ufi_mode_sense(disk);
	ufi_test_unit_ready(disk);

	ufi_prevent_allow_medium_removal(disk, 0);
	ufi_request_sense(disk);
	ufi_test_unit_ready(disk);

	ufi_read_capacity(disk);
	ufi_mode_sense(disk);
	ufi_mode_sense(disk);

	ufi_read10(disk, 0, 1);
	ufi_test_unit_ready(disk);
	ufi_test_unit_ready(disk);

	ufi_read10(disk, 0x001EBFFF, 1);

	return 0;
}

