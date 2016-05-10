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
#include "scsi.h"

struct cdb_6 {
	uint8_t lba[3];
	uint8_t transfer_length;
	uint8_t control;
} __attribute__((packed));

struct cdb_10 {
	uint8_t service_act;
	uint32_t lba;
	uint8_t info;
	uint16_t transfer_length;
	uint8_t control;
} __attribute__((packed));

struct cdb_12 {
	uint8_t service_act;
	uint32_t lba;
	uint32_t length;
	uint8_t info;
	uint8_t control;
} __attribute__((packed));

struct cdb_16 {
	uint8_t service_act;
	uint32_t lba;
	uint32_t additional_data;
	uint32_t length;
	uint8_t info;
	uint8_t control;
} __attribute__((packed));

struct cdb_16_long {
	uint8_t info;
	uint8_t lba[8];
	uint32_t transfer_length;
	uint8_t info1;
	uint8_t control;
} __attribute__((packed));

/* Command Descriptor Block */
struct scsi_cdb {
	uint8_t opcode;
	union {
		struct cdb_6 data_6;
		struct cdb_10 data_10;
		struct cdb_12 data_12;
		struct cdb_16 data_16;
		struct cdb_16_long data_16_long;
	};
} __attribute__((packed));

struct scsi_disk {
	usb_dev_t udev;
};

static void scsi_print_info(char *info)
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

static void scsi_test_unit_ready(struct scsi_disk *disk)
{
	int err;
	struct scsi_cdb cdb;
	struct xact data;

	memset(&cdb, 0, sizeof(struct scsi_cdb));

	/* Fill in the command */
	cdb.opcode = TEST_UNIT_READY;

	err = usb_storage_xfer(disk->udev, &cdb, sizeof(struct cdb_6) + 1, NULL, 0, 0);
	assert(!err);
}

static void scsi_request_sense(struct scsi_disk *disk, int desc)
{
	int err;
	struct scsi_cdb cdb;
	struct xact data;

	memset(&cdb, 0, sizeof(struct scsi_cdb));

	/* Fill in the command */
	cdb.opcode = REQUEST_SENSE;
	cdb.data_6.lba[0] = desc;
	cdb.data_6.transfer_length = 252;

	data.type = PID_IN;
	data.len = 252;
	err = usb_alloc_xact(disk->udev->dman, &data, 1);
	assert(!err);
	err = usb_storage_xfer(disk->udev, &cdb, sizeof(struct cdb_6) + 1,
				&data, 1, 1);
	assert(!err);
	usb_destroy_xact(disk->udev->dman, &data, 1);
}

static void scsi_inquiry(struct scsi_disk *disk)
{
	int err;
	struct scsi_cdb cdb;
	struct xact data;

	memset(&cdb, 0, sizeof(struct scsi_cdb));

	/* Inquiry SCSI disk */
	cdb.opcode = INQUIRY;
	cdb.data_6.control = 36;

	data.type = PID_IN;
	data.len = 36;
	err = usb_alloc_xact(disk->udev->dman, &data, 1);
	assert(!err);

	err = usb_storage_xfer(disk->udev, &cdb, sizeof(struct cdb_6) + 1,
				&data, 1, 1);
	assert(!err);

	scsi_print_info(xact_get_vaddr(&data));

	usb_destroy_xact(disk->udev->dman, &data, 1);
}

static void scsi_read6(struct scsi_disk *disk, uint32_t lba, uint8_t count)
{
	int err;
	struct scsi_cdb cdb;
	struct xact data;

	memset(&cdb, 0, sizeof(struct scsi_cdb));

	/* Inquiry SCSI disk */
	cdb.opcode = READ_6;
	cdb.data_6.lba[0] = (lba >> 16) & 0x1F;
	cdb.data_6.lba[1] = (lba >> 8) & 0xF;
	cdb.data_6.lba[2] = lba & 0xF;
	cdb.data_6.transfer_length = count;

	data.type = PID_IN;
	data.len = 512; //FIXME: should be lba * block size
	err = usb_alloc_xact(disk->udev->dman, &data, 1);
	assert(!err);

	err = usb_storage_xfer(disk->udev, &cdb, sizeof(struct cdb_6) + 1, &data, 1, 1);
	assert(!err);

	usb_destroy_xact(disk->udev->dman, &data, 1);
}

static void scsi_read_capacity10(struct scsi_disk *disk)
{
	int err;
	struct scsi_cdb cdb;
	struct xact data;

	memset(&cdb, 0, sizeof(struct scsi_cdb));

	cdb.opcode = READ_CAPACITY_10;

	data.type = PID_IN;
	data.len = 8;
	err = usb_alloc_xact(disk->udev->dman, &data, 1);
	assert(!err);

	err = usb_storage_xfer(disk->udev, &cdb, sizeof(struct cdb_10) + 1,
				&data, 1, 1);
	assert(!err);

	usb_destroy_xact(disk->udev->dman, &data, 1);
}

static void scsi_mode_sense6(struct scsi_disk *disk)
{
	int err;
	struct scsi_cdb cdb;
	struct xact data;

	memset(&cdb, 0, sizeof(struct scsi_cdb));

	cdb.opcode = MODE_SENSE_6;
	cdb.data_6.transfer_length = 36;

	data.type = PID_IN;
	data.len = 36;
	err = usb_alloc_xact(disk->udev->dman, &data, 1);
	assert(!err);

	err = usb_storage_xfer(disk->udev, &cdb, sizeof(struct cdb_6) + 1,
				&data, 1, 1);
	assert(!err);

	usb_destroy_xact(disk->udev->dman, &data, 1);
}

static void scsi_read10(struct scsi_disk *disk, uint32_t lba, uint16_t count)
{
	int err;
	struct scsi_cdb cdb;
	struct xact data[2];

	memset(&cdb, 0, sizeof(struct scsi_cdb));

	cdb.opcode = READ_10;
	cdb.data_10.lba = __builtin_bswap32(lba);
	cdb.data_10.transfer_length = __builtin_bswap16(count);

	data[0].type = PID_IN;
	data[0].len = 512 * count;

	data[1].type = PID_IN;
	data[1].len = 0;

	err = usb_alloc_xact(disk->udev->dman, &data, 2);
	assert(!err);

	err = usb_storage_xfer(disk->udev, &cdb, sizeof(struct cdb_10) + 1,
				&data, 2, 1);
	assert(!err);

	usb_destroy_xact(disk->udev->dman, &data, 2);
}

int
scsi_init_disk(usb_dev_t udev)
{
	struct scsi_disk *disk;

	disk = malloc(sizeof(struct scsi_disk));
	assert(disk);

	disk->udev = udev;

//	scsi_inquiry(disk);
	scsi_request_sense(disk, 0);
	scsi_test_unit_ready(disk);
	scsi_read_capacity10(disk);
	scsi_mode_sense6(disk);
	scsi_test_unit_ready(disk);
	scsi_read10(disk, 0, 1);

	return 0;
}

