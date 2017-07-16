#ifndef CDCNCM_DESCRIPTORS_H
#define CDCNCM_DESCRIPTORS_H

#include <stdint.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/cdc.h>

extern const struct usb_device_descriptor g_device_descriptor;
extern const struct usb_config_descriptor g_config_descriptor;

#define USBNET_USB_STRING_COUNT 4
extern const char *g_usb_strings[USBNET_USB_STRING_COUNT];

extern char g_usb_device_name[32];
extern char g_cdcecm_mac_address[13];

#define RNDIS_IRQ_EP  0x81
#define RNDIS_OUT_EP  0x02
#define RNDIS_IN_EP   0x83
#define CDCECM_IRQ_EP 0x84
#define CDCECM_OUT_EP 0x05
#define CDCECM_IN_EP  0x86

#define RNDIS_INTERFACE 0
#define CDCECM_INTERFACE 2

#endif
