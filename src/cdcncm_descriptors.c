#include "cdcncm_descriptors.h"
#include "cdcncm_std.h"

const struct usb_device_descriptor g_device_descriptor = {
        .bLength = USB_DT_DEVICE_SIZE,
        .bDescriptorType = USB_DT_DEVICE,
        .bcdUSB = 0x0200,
        .bDeviceClass = USB_CLASS_CDC,
        .bDeviceSubClass = 0,
        .bDeviceProtocol = 0,
        .bMaxPacketSize0 = 64,
        .idVendor = 0x0483,
        .idProduct = 0x5740,
        .bcdDevice = 0x0200,
        .iManufacturer = 1,
        .iProduct = 2,
        .iSerialNumber = 3,
        .bNumConfigurations = 1,
};

static const struct usb_endpoint_descriptor comm_endp[] = {{
        .bLength = USB_DT_ENDPOINT_SIZE,
        .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = CDCNCM_IRQ_EP,
        .bmAttributes = USB_ENDPOINT_ATTR_INTERRUPT,
        .wMaxPacketSize = 16,
        .bInterval = 100,
}};

static const struct usb_endpoint_descriptor data_endp[] = {{
        .bLength = USB_DT_ENDPOINT_SIZE,
        .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = CDCNCM_OUT_EP,
        .bmAttributes = USB_ENDPOINT_ATTR_BULK,
        .wMaxPacketSize = 64,
        .bInterval = 1,
}, {
        .bLength = USB_DT_ENDPOINT_SIZE,
        .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = CDCNCM_IN_EP,
        .bmAttributes = USB_ENDPOINT_ATTR_BULK,
        .wMaxPacketSize = 64,
        .bInterval = 1,
}};

static const struct {
        struct usb_cdc_header_descriptor header;
        struct usb_cdc_ncm_descriptor ncm;
        struct usb_cdc_enfd_descriptor enfd;
        struct usb_cdc_union_descriptor cdc_union;
} __attribute__((packed)) cdcncm_functional_descriptors = {
        .header = {
                .bFunctionLength = sizeof(struct usb_cdc_header_descriptor),
                .bDescriptorType = CS_INTERFACE,
                .bDescriptorSubtype = USB_CDC_TYPE_HEADER,
                .bcdCDC = 0x0110,
        },
        .ncm = {
                .bFunctionLength = sizeof(struct usb_cdc_ncm_descriptor),
                .bDescriptorType = CS_INTERFACE,
                .bDescriptorSubtype = USB_CDC_TYPE_NCM,
                .bcdNcmVersion = USB_CDC_NCM_VERSION,
                .bmNetworkCapabilities = 0,
        },
        .enfd = {
            .bFunctionLength = sizeof(struct usb_cdc_enfd_descriptor),
            .bDescriptorType = CS_INTERFACE,
            .bDescriptorSubtype = USB_CDC_TYPE_ENFD,
            .iMACAddress = 4, // In usb_strings table below
            .bmEthernetStatistics = 0,
            .wMaxSegmentSize = 1514,
            .wNumberMCFilters = 0,
            .bNumberPowerFilters = 0
        },
        .cdc_union = {
                .bFunctionLength = sizeof(struct usb_cdc_union_descriptor),
                .bDescriptorType = CS_INTERFACE,
                .bDescriptorSubtype = USB_CDC_TYPE_UNION,
                .bControlInterface = 0,
                .bSubordinateInterface0 = 1,
         },
};

static const struct usb_interface_descriptor comm_iface[] = {{
        .bLength = USB_DT_INTERFACE_SIZE,
        .bDescriptorType = USB_DT_INTERFACE,
        .bInterfaceNumber = 0,
        .bAlternateSetting = 0,
        .bNumEndpoints = 1,
        .bInterfaceClass = USB_CLASS_CDC,
        .bInterfaceSubClass = USB_CDC_SUBCLASS_NCM,
        .bInterfaceProtocol = 0,
        .iInterface = 0,

        .endpoint = comm_endp,

        .extra = &cdcncm_functional_descriptors,
        .extralen = sizeof(cdcncm_functional_descriptors),
}};

static const struct usb_interface_descriptor data_iface[] = {
{
        .bLength = USB_DT_INTERFACE_SIZE,
        .bDescriptorType = USB_DT_INTERFACE,
        .bInterfaceNumber = 1,
        .bAlternateSetting = 0,
        .bNumEndpoints = 0,
        .bInterfaceClass = USB_CLASS_DATA,
        .bInterfaceSubClass = 0,
        .bInterfaceProtocol = 0,
        .iInterface = 0,
},
{
        .bLength = USB_DT_INTERFACE_SIZE,
        .bDescriptorType = USB_DT_INTERFACE,
        .bInterfaceNumber = 1,
        .bAlternateSetting = 1,
        .bNumEndpoints = 2,
        .bInterfaceClass = USB_CLASS_DATA,
        .bInterfaceSubClass = 0,
        .bInterfaceProtocol = 0,
        .iInterface = 0,

        .endpoint = data_endp,
}
};

static uint8_t g_cdc_ncm_cur_altsetting;

static const struct usb_interface ifaces[] = {{
        .num_altsetting = 1,
        .altsetting = comm_iface,
}, {
        .num_altsetting = 2,
        .altsetting = data_iface,
        .cur_altsetting = &g_cdc_ncm_cur_altsetting
}};

const struct usb_config_descriptor g_config_descriptor = {
        .bLength = USB_DT_CONFIGURATION_SIZE,
        .bDescriptorType = USB_DT_CONFIGURATION,
        .wTotalLength = 0,
        .bNumInterfaces = 2,
        .bConfigurationValue = 1,
        .iConfiguration = 0,
        .bmAttributes = 0x80,
        .bMaxPower = 0x32,

        .interface = ifaces,
};

const char *g_usb_strings[] = {
        "devEmbedded",
        "DAQ4",
        "SERIALNUM",
        "001122334455"
};