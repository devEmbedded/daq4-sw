#ifndef USBNET_BUFFER_H
#define USBNET_BUFFER_H

#include <stdlib.h>
#include <libopencm3/usb/usbd.h>
#include "buffer.h"

#define USBNET_BUFFER_SIZE 768
#define USBNET_BUFFER_COUNT 4
#define USBNET_SMALLBUF_SIZE 128
#define USBNET_SMALLBUF_COUNT 2
#define USBNET_USB_PACKET_SIZE 64
#define USBNET_MAX_RX_QUEUE 1

/* Module initialization */
usbd_device *usbnet_init(const usbd_driver *driver, uint32_t serialnumber);

/* Returns true if the connection is ready.
 * Safe to call from IRQs.
 */
bool usbnet_is_connected();

/* Schedule a buffer for transmission.
 * Safe to call from IRQs.
 */
void usbnet_transmit(buffer_t *buffer);

/* Query current TX buffer size (number of queued frames).
 * 0 = line idle, 1 = one frame currently transmitting, 2+ = queued frames
 * Safe to call from IRQs.
 */
size_t usbnet_get_tx_queue_size();

/* Return a received buffer, or NULL.
 * Safe to call from IRQs.
 */
buffer_t *usbnet_receive();

/* Called by main thread for periodic processing. */
void usbnet_poll();

#endif

