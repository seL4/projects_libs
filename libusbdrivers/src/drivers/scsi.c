/*
 * Copyright 2015, NICTA
 *
 * This software may be distributed and modified according to the terms of
 * the BSD 2-Clause license. Note that NO WARRANTY is provided.
 * See "LICENSE_BSD2.txt" for details.
 *
 * @TAG(NICTA_BSD)
 */

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
	};
	union {
		struct cdb_10 data_10;
	};
	union {
		struct cdb_12 data_12;
	};
	union {
		struct cdb_16 data_16;
	};
	union {
		struct cdb_16_long data_16_long;
	};
} __attribute__((packed));

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

int
scsi_init_disk(usb_dev_t udev)
{
	int err;
	struct scsi_cdb cdb;
	struct xact data;

	memset(&cdb, 0, sizeof(struct scsi_cdb));

	/* Inquiry SCSI disk */
	cdb.opcode = INQUIRY;
	cdb.data_6.transfer_length = 36;

	data.type = PID_IN;
	data.len = 36;
	err = usb_alloc_xact(udev->dman, &data, 1);
	assert(!err);

	err = usb_storage_xfer(udev, &cdb, 6, &data, 1, 1);
	assert(!err);

	scsi_print_info(xact_get_vaddr(&data));

	usb_destroy_xact(udev->dman, &data, 1);

	return 0;
}

