#include "usbnet_descriptors.h"
#include "cdcecm_std.h"

static const struct usb_endpoint_descriptor rndis_irq_endp[] = {{
        .bLength = USB_DT_ENDPOINT_SIZE,
        .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = RNDIS_IRQ_EP,
        .bmAttributes = USB_ENDPOINT_ATTR_INTERRUPT,
        .wMaxPacketSize = 8,
        .bInterval = 1,
}};

static const struct usb_endpoint_descriptor rndis_data_endp[] = {{
        .bLength = USB_DT_ENDPOINT_SIZE,
        .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = RNDIS_IN_EP,
        .bmAttributes = USB_ENDPOINT_ATTR_BULK,
        .wMaxPacketSize = 64,
        .bInterval = 1,
}, {
        .bLength = USB_DT_ENDPOINT_SIZE,
        .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = RNDIS_OUT_EP,
        .bmAttributes = USB_ENDPOINT_ATTR_BULK,
        .wMaxPacketSize = 64,
        .bInterval = 1,
}};

static const struct usb_endpoint_descriptor cdcecm_irq_endp[] = {{
        .bLength = USB_DT_ENDPOINT_SIZE,
        .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = CDCECM_IRQ_EP,
        .bmAttributes = USB_ENDPOINT_ATTR_INTERRUPT,
        .wMaxPacketSize = 16,
        .bInterval = 100,
}};

static const struct usb_endpoint_descriptor cdcecm_data_endp[] = {{
        .bLength = USB_DT_ENDPOINT_SIZE,
        .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = CDCECM_IN_EP,
        .bmAttributes = USB_ENDPOINT_ATTR_BULK,
        .wMaxPacketSize = 64,
        .bInterval = 1,
}, {
        .bLength = USB_DT_ENDPOINT_SIZE,
        .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = CDCECM_OUT_EP,
        .bmAttributes = USB_ENDPOINT_ATTR_BULK,
        .wMaxPacketSize = 64,
        .bInterval = 1,
}};

static const struct {
        struct usb_cdc_header_descriptor header;
        struct usb_cdc_union_descriptor cdc_union;
        struct usb_cdc_enfd_descriptor enfd;
} __attribute__((packed)) cdcecm_functional_descriptors = {
        .header = {
                .bFunctionLength = sizeof(struct usb_cdc_header_descriptor),
                .bDescriptorType = CS_INTERFACE,
                .bDescriptorSubtype = USB_CDC_TYPE_HEADER,
                .bcdCDC = 0x0110,
        },
        .cdc_union = {
                .bFunctionLength = sizeof(struct usb_cdc_union_descriptor),
                .bDescriptorType = CS_INTERFACE,
                .bDescriptorSubtype = USB_CDC_TYPE_UNION,
                .bControlInterface = CDCECM_INTERFACE,
                .bSubordinateInterface0 = CDCECM_INTERFACE+1,
         },
        .enfd = {
            .bFunctionLength = sizeof(struct usb_cdc_enfd_descriptor),
            .bDescriptorType = CS_INTERFACE,
            .bDescriptorSubtype = USB_CDC_TYPE_ENFD,
            .iMACAddress = 4, // In usb_strings table below
            .bmEthernetStatistics = 0,
            .wMaxSegmentSize = 768,
            .wNumberMCFilters = 0,
            .bNumberPowerFilters = 0
        },
};


static const struct usb_iface_assoc_descriptor rndis_assoc = {
        .bLength = USB_DT_INTERFACE_ASSOCIATION_SIZE,
        .bDescriptorType = USB_DT_INTERFACE_ASSOCIATION,
        .bFirstInterface = RNDIS_INTERFACE,
        .bInterfaceCount = 2,
        .bFunctionClass = 0xEF,
        .bFunctionSubClass = 0x04,
        .bFunctionProtocol = 0x01,
        .iFunction = 0,
};

static const struct usb_interface_descriptor rndis_irq_iface[] = {{
        .bLength = USB_DT_INTERFACE_SIZE,
        .bDescriptorType = USB_DT_INTERFACE,
        .bInterfaceNumber = RNDIS_INTERFACE,
        .bAlternateSetting = 0,
        .bNumEndpoints = 1,
        .bInterfaceClass = USB_CLASS_CDC,
        .bInterfaceSubClass = USB_CDC_SUBCLASS_ACM,
        .bInterfaceProtocol = 0xFF,
        .iInterface = 0,

        .endpoint = rndis_irq_endp,
}};

static const struct usb_interface_descriptor rndis_data_iface[] = {
{
        .bLength = USB_DT_INTERFACE_SIZE,
        .bDescriptorType = USB_DT_INTERFACE,
        .bInterfaceNumber = RNDIS_INTERFACE + 1,
        .bAlternateSetting = 0,
        .bNumEndpoints = 2,
        .bInterfaceClass = USB_CLASS_DATA,
        .bInterfaceSubClass = 0,
        .bInterfaceProtocol = 0,
        .iInterface = 0,

        .endpoint = rndis_data_endp,
}};

static const struct usb_iface_assoc_descriptor cdcecm_assoc = {
        .bLength = USB_DT_INTERFACE_ASSOCIATION_SIZE,
        .bDescriptorType = USB_DT_INTERFACE_ASSOCIATION,
        .bFirstInterface = CDCECM_INTERFACE,
        .bInterfaceCount = 2,
        .bFunctionClass = USB_CLASS_CDC,
        .bFunctionSubClass = USB_CDC_SUBCLASS_ECM,
        .bFunctionProtocol = 0x00,
        .iFunction = 0,
};

static const struct usb_interface_descriptor cdcecm_irq_iface[] = {{
        .bLength = USB_DT_INTERFACE_SIZE,
        .bDescriptorType = USB_DT_INTERFACE,
        .bInterfaceNumber = CDCECM_INTERFACE,
        .bAlternateSetting = 0,
        .bNumEndpoints = 1,
        .bInterfaceClass = USB_CLASS_CDC,
        .bInterfaceSubClass = USB_CDC_SUBCLASS_ECM,
        .bInterfaceProtocol = 0,
        .iInterface = 0,

        .endpoint = cdcecm_irq_endp,

        .extra = &cdcecm_functional_descriptors,
        .extralen = sizeof(cdcecm_functional_descriptors),
}};

static const struct usb_interface_descriptor cdcecm_data_iface[] = {
{
        .bLength = USB_DT_INTERFACE_SIZE,
        .bDescriptorType = USB_DT_INTERFACE,
        .bInterfaceNumber = CDCECM_INTERFACE + 1,
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
        .bInterfaceNumber = CDCECM_INTERFACE + 1,
        .bAlternateSetting = 1,
        .bNumEndpoints = 2,
        .bInterfaceClass = USB_CLASS_DATA,
        .bInterfaceSubClass = 0,
        .bInterfaceProtocol = 0,
        .iInterface = 0,

        .endpoint = cdcecm_data_endp,
}
};

static uint8_t g_cdc_ncm_cur_altsetting;

static const struct usb_interface ifaces[] = {
{
  .num_altsetting = 1,
  .iface_assoc = &rndis_assoc,
  .altsetting = rndis_irq_iface,
},
{
  .num_altsetting = 1,
  .altsetting = rndis_data_iface,
},
{
  .num_altsetting = 1,
  .iface_assoc = &cdcecm_assoc,
  .altsetting = cdcecm_irq_iface,
},
{
  .num_altsetting = 2,
  .altsetting = cdcecm_data_iface,
  .cur_altsetting = &g_cdc_ncm_cur_altsetting
},
};

const struct usb_config_descriptor g_config_descriptor = {
        .bLength = USB_DT_CONFIGURATION_SIZE,
        .bDescriptorType = USB_DT_CONFIGURATION,
        .wTotalLength = 0,
        .bNumInterfaces = 4,
        .bConfigurationValue = 1,
        .iConfiguration = 0,
        .bmAttributes = 0x80,
        .bMaxPower = 0x32,

        .interface = ifaces,
};

const struct usb_device_descriptor g_device_descriptor = {
        .bLength = USB_DT_DEVICE_SIZE,
        .bDescriptorType = USB_DT_DEVICE,
        .bcdUSB = 0x0200,
        .bDeviceClass = 0, /* 0 = Use class info from interface descriptors */
        .bDeviceSubClass = 0,
        .bDeviceProtocol = 0,
        .bMaxPacketSize0 = 64,
        .idVendor = 0x1d6b,
        .idProduct = 0x0129,
        .bcdDevice = 0x0205,
        .iManufacturer = 1,
        .iProduct = 2,
        .iSerialNumber = 3,
        .bNumConfigurations = 1,
};

char g_usb_device_name[32];
char g_cdcecm_mac_address[13];

const char *g_usb_strings[USBNET_USB_STRING_COUNT] = {
        "devEmbedded",
        g_usb_device_name,
        g_cdcecm_mac_address, /* Serial number */
        g_cdcecm_mac_address /* MAC address for CDC ECM */
};