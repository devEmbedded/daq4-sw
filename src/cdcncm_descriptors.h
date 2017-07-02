#ifndef CDCNCM_DESCRIPTORS_H
#define CDCNCM_DESCRIPTORS_H

#include <stdint.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/cdc.h>

extern const struct usb_device_descriptor g_device_descriptor;
extern const struct usb_config_descriptor g_config_descriptor;

#define CDCNCM_USB_STRING_COUNT 4
extern const char *g_usb_strings[CDCNCM_USB_STRING_COUNT];

#define CDCNCM_IRQ_EP 0x83
#define CDCNCM_OUT_EP 0x01
#define CDCNCM_IN_EP  0x82

#endif
