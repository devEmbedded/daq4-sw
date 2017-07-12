#ifndef USBNET_BUFFER_H
#define USBNET_BUFFER_H

#include <stdlib.h>
#include <libopencm3/usb/usbd.h>

#define USBNET_BUFFER_SIZE 768
#define USBNET_BUFFER_COUNT 4
#define USBNET_SMALLBUF_SIZE 256
#define USBNET_SMALLBUF_COUNT 2
#define USBNET_USB_PACKET_SIZE 64
#define USBNET_MAX_RX_QUEUE 1

typedef struct _usbnet_buffer_t {
    struct _usbnet_buffer_t *next;
    const uint16_t max_size;
    uint16_t data_size;
    uint8_t data[];
} usbnet_buffer_t;

/* Module initialization */
usbd_device *usbnet_init(const usbd_driver *driver);

/* Functions below are safe to use from IRQs. */

bool usbnet_is_connected();

/* Allocate a buffer for use for formatting a frame to be transmitted.
 * Returns NULL if all buffers are in use. */
usbnet_buffer_t *usbnet_allocate(size_t size);

/* Schedule a buffer for transmission. */
void usbnet_transmit(usbnet_buffer_t *buffer);

/* Query current TX buffer size (number of queued frames) */
size_t usbnet_get_tx_queue_size();

/* Return a received buffer, or NULL. */
usbnet_buffer_t *usbnet_receive();

/* Free a received or otherwise allocated buffer. */
void usbnet_release(usbnet_buffer_t *buffer);

#endif

