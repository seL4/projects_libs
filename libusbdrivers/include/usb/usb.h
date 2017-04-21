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

#ifndef _USB_USB_H_
#define _USB_USB_H_


#include <platsupport/io.h>
#include <usb/usb_host.h>

// Maximum number of devices a host can manage
#define USB_NDEVICES 32

// Maximum number of endpoints per device
#define USB_MAX_EPS 16

// Maximum size of a string descriptor
#define MAX_STRING_SIZE 255

struct usb_dev;
typedef struct usb_dev* usb_dev_t;

struct usb {
    usb_host_t hdev;
    /// Bitmap representation of used addresses
    uint32_t addrbm;
    /// Next address: delays address recycling
    int next_addr;
    /// List of devices connected to this host
    usb_dev_t devlist;
};
typedef struct usb usb_t;


enum usb_class {
    USB_CLASS_UNSPECIFIED = 0x00,
    USB_CLASS_AUDIO       = 0x01,
    USB_CLASS_COMM        = 0x02,
    USB_CLASS_HID         = 0x03,
    /* ------------------------*/
    USB_CLASS_PID         = 0x05,
    USB_CLASS_IMAGE       = 0x06,
    USB_CLASS_PRINTER     = 0x07,
    USB_CLASS_STORAGE     = 0x08,
    USB_CLASS_HUB         = 0x09,
    USB_CLASS_CDCDATA     = 0x0A,
    USB_CLASS_CARDREADER  = 0x0B,
    /* ------------------------*/
    USB_CLASS_SECURITY    = 0x0D,
    USB_CLASS_VIDEO       = 0x0E,
    USB_CLASS_HEALTH      = 0x0F,
    USB_CLASS_AV          = 0x10,
    /* ------------------------*/
    USB_CLASS_DIAGNOSTIC  = 0xDC,
    /* ------------------------*/
    USB_CLASS_WIRELESS    = 0xE0,
    /* ------------------------*/
    USB_CLASS_MISC        = 0xEF,
    /* ------------------------*/
    USB_CLASS_APPSPEC     = 0xFE,
    USB_CLASS_VEND        = 0xFF
};

struct usb_dev {
    /* Filled on creation */
    usb_t *host;
    usb_dev_t hub;
    uint8_t  port;
    uint8_t tt_addr;
    uint8_t tt_port;
    enum usb_speed speed;
    ps_dma_man_t* dman;
    /* Filled on creation/init */
    uint16_t prod_id;
    uint16_t vend_id;
    uint8_t  class;
    uint8_t  addr;
    /* Filled by driver */
    int (*connect)(struct usb_dev* udev);
    int (*disconnect)(struct usb_dev* udev);
    struct udev_priv    *dev_data;
    /*
     * A device can have up to 16 endpoints(only 4 for low speed devices),
     * control endpoint is separate and is shared by all interfaces.
     */
    struct endpoint *ep_ctrl;         // Control endpoint of the device
    struct endpoint *ep[USB_MAX_EPS]; // The endpoints of the device

    /* For device lists */
    struct usb_dev      *next;
};

/*
 * USB requests
 */

/* Data transfer direction, bit 7 of bmRequestType */
#define USB_DIR_OUT          0
#define USB_DIR_IN           BIT(7)

/* Request type, bit 6-5 of bmRequestType */
#define USB_TYPE_STD         (0 * BIT(5))
#define USB_TYPE_CLS         (1 * BIT(5))
#define USB_TYPE_VEN         (2 * BIT(5))

/* Request recipient, bit 4-0 of bmRequestType */
#define USB_RCPT_DEVICE      0
#define USB_RCPT_INTERFACE   1
#define USB_RCPT_ENDPOINT    2
#define USB_RCPT_OTHER       3

/* bRequest */
enum Request {
    GET_STATUS        = 0,
    CLR_FEATURE       = 1,
    SET_FEATURE       = 3,
    SET_ADDRESS       = 5,
    GET_DESCRIPTOR    = 6,
    SET_DESCRIPTOR    = 7,
    GET_CONFIGURATION = 8,
    SET_CONFIGURATION = 9,
    GET_INTERFACE     = 10,
    SET_INTERFACE     = 11,
    SYNCH_FRAME       = 12
};

/*** bmRequestType ***/
struct usbreq {
    uint8_t bmRequestType;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} __attribute__ ((packed));


/*
 * USB descriptors
 */
/* bDescriptorType */
enum DescriptorType {
    DEVICE                    = 0x01,
    CONFIGURATION             = 0x02,
    STRING                    = 0x03,
    INTERFACE                 = 0x04,
    ENDPOINT                  = 0x05,
    DEVICE_QUALIFIER          = 0x06,
    OTHER_SPEED_CONFIGURATION = 0x07,
    INTERFACE_POWER           = 0x08,
    /* Class specific types */
    HID                       = 0x21,
    HID_REPORT                = 0x22,
    HID_PHYSICAL              = 0x23,
    CS_INTERFACE              = 0x24, //USB class-specific
    CS_ENDPOINT               = 0x25, //USB class-specific
    HUB                       = 0x29
};

struct device_desc {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations;
} __attribute__ ((packed));

struct device_qualifier_desc {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint8_t  bNumConfigurations;
    uint8_t  bReserved;
} __attribute__ ((packed));

/* Descriptors */
struct anon_desc {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
} __attribute__ ((packed));

struct config_desc {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t wTotalLength;
    uint8_t  bNumInterfaces;
    uint8_t  bConfigurationValue;
    uint8_t  iConfigurationIndex;
    uint8_t  bmAttributes;
    uint8_t  bMaxPower;
} __attribute__ ((packed));

struct iface_desc {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bInterfaceNumber;
    uint8_t bAlternateSetting;
    uint8_t bNumEndpoints;
    uint8_t bInterfaceClass;
    uint8_t bInterfaceSubClass;
    uint8_t bInterfaceProtocol;
    uint8_t iInterface;
} __attribute__ ((packed));

struct endpoint_desc {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress;
    uint8_t  bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
} __attribute__ ((packed));

struct string_desc {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bString[MAX_STRING_SIZE];
} __attribute__ ((packed));

/* Class HID */
struct hid_desc {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t bcdHID;
    uint8_t bCountryCode;
    uint8_t bNumDescriptors;
    uint8_t bReportDescriptorType;
    uint16_t wReportDescriptorLength;
} __attribute__ ((packed));


static inline struct usbreq
__get_descriptor_req(enum DescriptorType t, int value, int index, int size) {
    struct usbreq r = {
        .bmRequestType = (USB_DIR_IN | USB_TYPE_STD | USB_RCPT_DEVICE),
        .bRequest      = GET_DESCRIPTOR,
        .wValue        = (t << 8) + value,
        .wIndex        = index,
        .wLength       = size
    };
    return r;
}

static inline struct usbreq
__set_configuration_req(int index) {
    struct usbreq r = {
        .bmRequestType = (USB_DIR_OUT | USB_TYPE_STD | USB_RCPT_DEVICE),
        .bRequest      = SET_CONFIGURATION,
        .wValue        = index,
        .wIndex        = 0,
        .wLength       = 0
    };
    return r;
}

static inline struct usbreq
__set_interface_req(int index) {
    struct usbreq r = {
        .bmRequestType = (USB_DIR_OUT | USB_TYPE_STD | USB_RCPT_INTERFACE),
        .bRequest      = SET_INTERFACE,
        .wValue        = 0,
        .wIndex        = index,
        .wLength       = 0
    };
    return r;
}

/****************************************/

/** Initialise usb
 * @param[in] id            the ID of the USB host to
 *                          initialise
 * @param[in] ioops         a structure defining operations for
 *                          device access.
 * @param[in] sync          a structure defining operations for
 *                          synchronization operations access.
 * @param[out] host         On success, this will be filled with
 *                          a handle to the usb host controller
 *                          associated with @ref{id}.
 * @return                  0 on success.
 */
int usb_init(enum usb_host_id id, ps_io_ops_t* ioops, sync_ops_t *sync, usb_t* host);

/** Probe for a new device on the BUS.
 * This function is typically called by a HUB device when it
 * detects a new connection. The new device inherits the DMA
 * allocator of host device. On success, key device parameters
 * will have been cached and the device will have been assigned
 * a new address on the bus.
 * @param[in] hub   The USB hub that the new device is connected
 *                  to.
 * @param[in] port  The port on the provided hub that the new
 *                  device is connected to.
 * @param[in] speed The connection speed of the new device.
 * @param[out] d    On success, d will contain a reference to
 *                  the new device.
 * @return          0 on success.
 */
int usb_new_device(usb_dev_t hub, int port,
                   enum usb_speed speed, usb_dev_t* d);

/** A call back for the configuration parser. This function is
 * called for each descriptor.
 * @param[in] token An unmodified token as passed to the
 *                  configuration parser.
 * @param[in] cfg   The current configuration number. -1 for
 *                  none.
 * @param[in] iface The current interface number. -1 for none.
 * @param[in] desc  The current descriptor to be parsed.
 * @return          0 for next descriptor, otherwise an error
 *                  code to return to the caller.
 */
typedef int (*usb_config_cb)(void* token, int cfg, int iface,
                             struct anon_desc* desc);

/** Iteratively passed all descriptors to a caller provided
 * function.
 * Configuring a device is tedious because not only do
 * all descriptors come as a giant blob, but they may not
 * be aligned correctly and must be copied to aligned memory
 * before use.
 */
int usbdev_parse_config(usb_dev_t udev, usb_config_cb cb,
                        void* token);

/** Disconnect a USB device. Usually a call to this function
 * is driven by the physical disconnection of the device
 * from the bus.
 * @param[in] dev  A reference to the device to disconnect
 */
void usbdev_disconnect(usb_dev_t dev);

/** Return the class reported by a USB device
 * @param[in] The USB device in question
 * @return    The class ID
 */
enum usb_class usbdev_get_class(usb_dev_t dev);

/**
 * Returns a string representation of the provided USB class code.
 * the string contains no leading or trailing white space and does
 * not end in a new line character.
 * @param[in] usb_class the class id in question.
 * @return              a string representation of the USB class.
 */
const char* usb_class_get_description(enum usb_class usb_class);

/** Schedule a transaction on the provided USB device
 * @param[in] udev    The USB device which is to receive the
 *                    transaction.
 * @param[in] ep      The endpoint to deliver the
 *                    packets to.
 * @param[in] xact    A structure describing the packets to be
 *                    sent.
 * @param[in] nxact   The number of entries in the xact
 *                    structure to use.
 * @param[in] cb      A call back function to call once the
 *                    packet is delivered or if there was a
 *                    transmission error. NULL if blocking
 *                    behaviour is required.
 * @param[in] token   Passed unmodified to the call back
 *                    function.
 * @return            0 on success.
 */
int usbdev_schedule_xact(usb_dev_t udev, struct endpoint *ep, struct xact* xact,
                         int nxact, usb_cb_t cb, void* token);


/** Print a list of registered devices
 * @param[in] host  the USB host device in question
 * @param[in] v     the level of information to print.
 */
void usb_lsusb(usb_t* host, int v);

/** Retrieve a USB device from the bus
 * @param[in] host The USB host device that the USB device is attached to.
 * @param[in] addr The address of the USB device requested.
 * @return         A handle to the device or NULL if there is no device at
 *                 that address.
 */
usb_dev_t usb_get_device(usb_t* host, int addr);

/** Probes and prints the descriptors for the given device
 * @param[in] dev  The usb device to probe
 */
void usb_probe_device(usb_dev_t dev);

/** Pass control to the devices IRQ handler
 * @param[in] host    The USB host that triggered
 *                    the interrupt event.
 */
void usb_handle_irq(usb_t* host);

/** Allocate transaction buffers for requests
 * @param[in]     dman   A dma allocator instance
 * @param[in/out] xact   A array structure which provides the sizes of buffers
 *                       to allocate. On success, this array will be filled
 *                       with references to the allocated DMA memory.
 * @param[in]    nxact   The size, in array indexes, of xact.
 * @return               0 on success
 */
int usb_alloc_xact(ps_dma_man_t* dman, struct xact* xact, int nxact);

/** Frees transaction buffers
 * @param[in]    dman  The dma allocator instance that was used for the allocation
 * @param[in]    xact  The description of the transaction
 * @param[in]    nxact The number of entries in xact
 */
void usb_destroy_xact(ps_dma_man_t* dman, struct xact* xact, int nxact);

#endif /* _USB_USB_H_ */
