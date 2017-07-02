#ifndef CDCNCM_STD_H
#define CDCNCM_STD_H

#include <libopencm3/usb/cdc.h>

#pragma pack(push,1)

/* USB CDC NCM standard definitions, from ECM120 and NCM10 standards */

/* Class Subclass Code */
#define USB_CDC_SUBCLASS_NCM  0x0D

/* bDescriptor SubType */
#define USB_CDC_TYPE_ENFD     0x0F
#define USB_CDC_TYPE_NCM      0x1A

/* Class-specific notification codes */
#define USB_CDC_NOTIFY_NETWORK_CONNECTION       0x00
#define USB_CDC_NOTIFY_CONNECTION_SPEED_CHANGE  0x2A

/* Table 3: Ethernet Networking Functional Descriptor */
struct usb_cdc_enfd_descriptor {
        uint8_t bFunctionLength;        /* Size of structure */
        uint8_t bDescriptorType;        /* CS_INTERFACE */
        uint8_t bDescriptorSubtype;     /* USB_CDC_TYPE_ENFD */
        uint8_t iMACAddress;            /* Index of string descriptor */
        uint32_t bmEthernetStatistics;  /* Bitmap of supported statistics */
        uint16_t wMaxSegmentSize;       /* MTU of the link, typically 1514 */
        uint16_t wNumberMCFilters;      /* Supported multicast filters */
        uint8_t bNumberPowerFilters;    /* Supported wakeup filters */
};

/* Table 5-2: NCM Functional Descriptor */
struct usb_cdc_ncm_descriptor {
        uint8_t bFunctionLength;        /* Size of structure */
        uint8_t bDescriptorType;        /* CS_INTERFACE */
        uint8_t bDescriptorSubtype;     /* USB_CDC_TYPE_NCM */
        uint16_t bcdNcmVersion;         /* USB_CDC_NCM_VERSION */
        uint8_t bmNetworkCapabilities;  /* Bitmask of capabilities */
};

#define USB_CDC_NCM_VERSION 0x0100

/* Table 6-1: Networking Control Model Requests */
#define USB_CDC_REQ_GET_NTB_PARAMETERS  0x80
#define USB_CDC_REQ_GET_NTB_INPUT_SIZE  0x85
#define USB_CDC_REQ_SET_NTB_INPUT_SIZE  0x86

/* Table 6-3: NTB Parameter Structure */
struct usb_cdc_ncm_ntb_parameters {
        uint16_t wLength;               /* Size of structure */
        uint16_t bmNtbFormatsSupported; /* Supported formats (16/32 bit) */
        uint32_t dwNtbInMaxSize;        /* IN NTB maximum size */
        uint16_t wNdpInDivisor;         /* IN NTB datagram alignment divisor */
        uint16_t wNdpInPayloadRemainder;/* IN NTB datagram alignment modulus */
        uint16_t wNdpInAlignment;       /* IN NTB NDP alignment */
        uint16_t wReserved;             /* Keep as 0 */
        uint32_t dwNtbOutMaxSize;       /* OUT NTB maximum size */
        uint16_t wNdpOutDivisor;        /* OUT NTB datagram alignment divisor */
        uint16_t wNdpOutPayloadRemainder;/* OUT NTB datagram alignment modulus*/
        uint16_t wNdpOutAlignment;       /* OUT NTB NDP alignment */
        uint16_t wNtbOutMaxDatagrams;   /* OUT NTB maximum number of datagrams*/
};

/* Section 6.3 table 21: ConnectionSpeedChange Data Structure */
struct usb_cdc_ncm_connection_speed {
        struct usb_cdc_notification cdc_header;
        uint32_t DLBitRate;
        uint32_t ULBitRate;
};

/* Section 3.2 table 3-1: 16-bit NCM transfer header (NTH16) */
struct usb_cdc_ncm_nth16 {
        uint32_t dwSignature;           /* USB_CDC_NCM_NTH16_SIGNATURE */
        uint16_t wHeaderLength;         /* Size of structure */
        uint16_t wSequence;             /* Sequence number */
        uint16_t wBlockLength;          /* Block length, or 0 if ends in ZLP */
        uint16_t wNdpIndex;             /* Offset of first NDP16 struct */
};

/* Section 3.3 table 3-3: 16-bit NCM datagram pointer table (NDP16) */
struct usb_cdc_ncm_ndp16_header {
        uint32_t dwSignature;           /* USB_CDC_NCM_NDP16_SIGNATURE_NOCRC */
        uint16_t wLength;               /* Size of header + datagram pointers */
        uint16_t wReserved;             /* Keep as 0 */
};

/* The NDP16 header is followed by 1..N datagram pointers, and then a
 * terminating pointer with both fields set to 0. */
struct usb_cdc_ncm_ndp16_pointer {
        uint16_t wDatagramIndex;        /* Offset from NTH16 start */
        uint16_t wDatagramLength;       /* Length in bytes */
};

struct usb_cdc_ncm_ndp16 {
  struct usb_cdc_ncm_ndp16_header hdr;
  struct usb_cdc_ncm_ndp16_pointer datagrams[];
};

#define USB_CDC_NCM_NTH16_SIGNATURE             0x484D434E
#define USB_CDC_NCM_NDP16_SIGNATURE_NOCRC       0x304D434E

#pragma pack(pop)

#endif

