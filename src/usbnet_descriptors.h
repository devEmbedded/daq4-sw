#ifndef CDCNCM_DESCRIPTORS_H
#define CDCNCM_DESCRIPTORS_H

#include <stdint.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/cdc.h>

extern const struct usb_device_descriptor g_device_descriptor;
extern const struct usb_config_descriptor g_config_descriptor;

#define USBNET_USB_STRING_COUNT 4
extern const char *g_usb_strings[USBNET_USB_STRING_COUNT];

#define RNDIS_IRQ_EP  0x82
#define RNDIS_OUT_EP  0x01
#define RNDIS_IN_EP   0x83
#define CDCECM_IRQ_EP 0x84
#define CDCECM_OUT_EP 0x06
#define CDCECM_IN_EP  0x85

#define RNDIS_INTERFACE 0

#endif
