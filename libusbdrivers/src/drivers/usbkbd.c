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
 * @brief USB Keyboard Driver
 * @see USB HID spec, Appendix B
 */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "../services.h"
#include "hid.h"
#include "usbkbd.h"

#define KBD_ENABLE_IRQS

#define KBD_REPEAT_RATE_MS   200
#define KBD_REPEAT_DELAY_MS 1000

/*
 * Ring buffer for key logging
 */

struct ringbuf {
	char b[100];
	int head;
	int tail;
	int size;
};

static void rb_init(struct ringbuf *rb)
{
	rb->head = 0;
	rb->tail = 0;
	rb->size = 0;
}

static int rb_produce(struct ringbuf *rb, const char *str, int size)
{
	if (sizeof(rb->b) / sizeof(*rb->b) - rb->size >= size) {
		while (size--) {
			rb->b[rb->tail++] = *str++;
			if (rb->tail == sizeof(rb->b) / sizeof(*rb->b)) {
				rb->tail = 0;
			}
			rb->size++;
		}
		return 0;
	} else {
		return 1;
	}
}

static int rb_consume(struct ringbuf *rb)
{
	char c;
	if (rb->size) {
		c = rb->b[rb->head++];
		if (rb->head == sizeof(rb->b) / sizeof(*rb->b)) {
			rb->head = 0;
		}
		rb->size--;
		return ((int)c) & 0xff;
	}
	return -1;
}

/*
 * Keyboard driver
 */

#define KBDRPT_RATE  ((KBD_REPEAT_RATE_MS + 3) / 4)
#define KBDRPT_DELAY ((KBD_REPEAT_DELAY_MS + 3) / 4)
#define KBD_PROTOCOL 1
#define KBD_KEYS_SIZE   8

#define KBDIND_CAPS    0x2
#define KBDIND_NUM     0x1
#define KBDIND_SCRL    0x4

struct usb_kbd_device {
	struct usb_dev *udev;
	struct usb_hid_device *hid;
	struct endpoint *ep_int;
	struct xact int_xact;
/// Indicator state. This is a pointer to our universal buffer at index 1
	uint8_t ind;
/// Store old keys for repeat detection
	uint8_t old_keys[KBD_KEYS_SIZE];
/// new keys is a pointer to our interrupt buffer
	uint8_t *new_keys;
/// Cache the current repeat rate to avoid USB transfers
	int repeat_rate;
/// ring buffer for characters waiting to be read by the application.
	struct ringbuf rb;
};

#define KBDFN_CTRL   0x01
#define KBDFN_SHIFT  0x02
#define KBDFN_ALT    0x04
#define KBDFN_WIN    0x08
#define KBDFN_LEFT(FN)     ((KBDFN_##FN) << 0)
#define KBDFN_RIGHT(FN)    ((KBDFN_##FN) << 4)
#define KBDFN_TEST(FN, x)  !!((x) & (KBDFN_LEFT(FN) | KBDFN_RIGHT(FN)))
static const char std_kcodes[] = {
	/*       0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F */
	/*0x00 */ 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l',
	/*0x10 */ 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', '1', '2',
	/*0x20 */ '3', '4', '5', '6', '7', '8', '9', '0', 10, 27, 8, 9, ' ', '-', '=', '[',
	/*0x30 */ ']', 92, 0, ';', 39, '`', ',', '.', '/'
};

static const char stdshift_kcodes[] = {
	/*       0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F */
	/*0x00 */ 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L',
	/*0x10 */ 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', '!', '@',
	/*0x20 */ '#', '$', '%', '^', '&', '*', '(', ')', 10, 27, 8, 9, ' ', '_', '+', '{',
	/*0x30 */ '}', '|', 0, ':', 39, '~', '<', '>', '?'
};

static const char num_kcodes[] = {
	/*       0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F */
	/*0x50 */ '/', '*', '-', '+', 10, '1', '2', '3', '4', '5', '6', '7',
	/*0x60 */ '8', '9', '0', '.'
};

#define KBDKEY_NONE       0x00
#define KBDKEY_MASH       0x01
#define KBDKEY_NUMLOCK    0x53
#define KBDKEY_CAPSLOCK   0x39
#define KBDKEY_SCROLLLOCK 0x47

static int kbd_update_ind(struct usb_kbd_device *kbd)
{
	int err;

	err = usb_hid_set_report(kbd->hid, REPORT_OUTPUT, &kbd->ind, 1);

	return err;
}

static int kbd_update_repeat_rate(struct usb_kbd_device *kbd)
{
	ZF_LOGD("Changing rate to %dms\n", kbd->repeat_rate * 4);

	return usb_hid_set_idle(kbd->hid, kbd->repeat_rate);
}

static int
kbd_irq_handler(void *token, enum usb_xact_status stat, int bytes_remaining)
{
	struct usb_kbd_device *kbd = (struct usb_kbd_device *)token;
	uint8_t afn;
	uint8_t key;
	int new_rate = -1;
	char c;
	int len;

	/* Check the status */
	if (stat != XACTSTAT_SUCCESS) {
		ZF_LOGD("Received unsuccessful IRQ\n");
		return 1;
	}
	len = kbd->int_xact.len - bytes_remaining;
	if (len < 4) {
		ZF_LOGD("Short read on INT packet (%d)\n", len);
		return 1;
	}
#if defined(KBDIRQ_DEBUG)
	{
		int i;
		for (i = 0; i < len; i++) {
			printf("[0x%02x]", kbd->new_keys[i]);
		}
		printf("\n");
	}
#endif

	/* Multiple key press. Ignore input */
	if (kbd->new_keys[3] != KBDKEY_NONE) {
		kbd->new_keys[2] = kbd->old_keys[2] = KBDKEY_NONE;
	}
	/* Read in key parameters */
	afn = kbd->new_keys[0];
	key = kbd->new_keys[2];
	/* Handle repeat delay */
	if (key == KBDKEY_NONE) {
		/* No key pressed or someone is being a jerk - idle */
		new_rate = 0;
	} else if (kbd->old_keys[2] == key) {
		/* Someone is holding down a key */
		new_rate = KBDRPT_RATE;
	} else {
		/* Someone pressed a new key! Start repeat delay */
		new_rate = KBDRPT_DELAY;
	}
	/* Adjust the idle delay if required */
	if (new_rate != kbd->repeat_rate) {
		kbd->repeat_rate = new_rate;
		kbd_update_repeat_rate(kbd);
	}
	/* Process the key */
	memcpy(kbd->old_keys, kbd->new_keys, KBD_KEYS_SIZE);
	if (key == KBDKEY_NONE || key == KBDKEY_MASH) {
		/* Ignore it */
	} else if (key < 0x04) {
		printf("<!0x%x>", key);
	} else if (key < 0x39) {
		char cl;
		/* Decode the key */
		c = std_kcodes[key - 0x04];
		if (KBDFN_TEST(CTRL, afn) && c >= '@' && c <= '_') {
			c -= '@';
		} else if (KBDFN_TEST(SHIFT, afn)) {
			c = stdshift_kcodes[key - 0x04];
		}
		/* Check and update for capslock */
		cl = c | 0x20;
		if (cl >= 'a' && cl <= 'z' && (kbd->ind & KBDIND_CAPS)) {
			c ^= 0x20;
		}
		/* Register the character */
		if (KBDFN_TEST(ALT, afn)) {
			char str[2] = { 0x1B, c };
			rb_produce(&kbd->rb, str, 2);
		} else {
			rb_produce(&kbd->rb, &c, 1);
		}
	} else if (key == KBDKEY_NUMLOCK) {
		kbd->ind ^= KBDIND_NUM;
		kbd_update_ind(kbd);
	} else if (key == KBDKEY_CAPSLOCK) {
		kbd->ind ^= KBDIND_CAPS;
		kbd_update_ind(kbd);
	} else if (key == KBDKEY_SCROLLLOCK) {
		kbd->ind ^= KBDIND_SCRL;
		kbd_update_ind(kbd);
	} else if (key < 0x54) {
		/* TODO handle these codes (see below) */
		printf("<!0x%x>", key);
	} else if (key < 0x64 && (kbd->ind & KBDIND_NUM)) {
		c = num_kcodes[key - 0x54];
		rb_produce(&kbd->rb, &c, 1);
	} else if (key < 0x66) {
		/* TODO find scan codes for these keys and keypad
		 * with no numlock */
#if 0
		F1 - F12 3 a - 45
		    prntscrn / sysrq 46
		    pause / break 48
		    insert 49 home 4 a pgup 4 b delete 4 c end 4 d pgdwn 4e
 right 4f left 50 down 51 up 52 macro 64 dropdown 65
#endif
		    printf("<!0x%x>", key);
	} else {
		printf("<!!0x%x>", key);
	}

	usbdev_schedule_xact(kbd->udev, kbd->ep_int, &kbd->int_xact, 1,
			     &kbd_irq_handler, kbd);

	return 1;
}

static ssize_t
kbd_read(ps_chardevice_t *d, void *vdata, size_t bytes,
	 chardev_callback_t cb, void *token)
{
	struct usb_kbd_device *kbd;
	char *data;
	int i;
	kbd = (struct usb_kbd_device *)d->vaddr;
	data = (char *)vdata;
	for (i = 0; i < bytes; i++) {
		int c;
		c = rb_consume(&kbd->rb);
		if (c >= 0) {
			*data++ = c;
		} else {
			break;
		}
	}
	return i;
}

int usb_kbd_driver_bind(usb_dev_t *usb_dev, struct ps_chardevice *cdev)
{
	struct usb_kbd_device *kbd;
	int err;

	kbd = (struct usb_kbd_device*)usb_malloc(sizeof(struct usb_kbd_device));
	if (!kbd) {
		ZF_LOGE("Out of memory\n");
		abort();
	}

	usb_dev->dev_data = (struct udev_priv*)kbd;
	kbd->udev = usb_dev;
	kbd->repeat_rate = 0;
	rb_init(&kbd->rb);

	kbd->hid = usb_hid_alloc(usb_dev);

	if (kbd->hid->protocol != 1) {
		ZF_LOGE("Not a keyboard: %d\n", kbd->hid->protocol);
		abort();
	}

	/* Find endpoint */
	kbd->ep_int = usb_dev->ep[kbd->hid->iface];
	if (kbd->ep_int == NULL || kbd->ep_int->type != EP_INTERRUPT) {
		ZF_LOGF("Endpoint not found\n");
		abort();
	}

	ZF_LOGD("Configuring keyboard\n");

	err = usb_hid_set_idle(kbd->hid, 0);
	if (err < 0) {
		ZF_LOGD("Keyboard initialisation error\n");
		abort();
	}

	cdev->vaddr = kbd;
	cdev->read = kbd_read;

	/* Initialise LEDS */
	kbd_update_ind(kbd);

	/* Initialise IRQs */
	if (kbd->ep_int->dir == EP_DIR_IN) {
		kbd->int_xact.type = PID_IN;
	} else {
		kbd->int_xact.type = PID_OUT;
	}

	kbd->int_xact.len = kbd->ep_int->max_pkt;

	err = usb_alloc_xact(usb_dev->dman, &kbd->int_xact, 1);
	if (err) {
		ZF_LOGF("Out of DMA memory\n");
		abort();
	}

	kbd->new_keys = xact_get_vaddr(&kbd->int_xact);

#if defined(KBD_ENABLE_IRQS)
	ZF_LOGD("Scheduling IRQS\n");
	usbdev_schedule_xact(usb_dev, kbd->ep_int, &kbd->int_xact, 1,
			     &kbd_irq_handler, kbd);
#else
	(void)kbd_irq_handler;
#endif
	ZF_LOGD("Successfully initialised\n");

	return 0;
}
