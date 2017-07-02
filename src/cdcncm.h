#ifndef CDCNCM_BUFFER_H
#define CDCNCM_BUFFER_H

#include <stdlib.h>
#include <libopencm3/usb/usbd.h>

#define CDCNCM_BUFFER_SIZE 768
#define CDCNCM_BUFFER_COUNT 4
#define CDCNCM_USB_PACKET_SIZE 64
#define CDCNCM_MAX_RX_QUEUE 1

typedef struct _cdcncm_buffer_t {
    struct _cdcncm_buffer_t *next;
    size_t data_size;
    uint8_t data[CDCNCM_BUFFER_SIZE];
} cdcncm_buffer_t;

/* Module initialization */
usbd_device *cdcncm_init(const usbd_driver *driver);

/* Functions below are safe to use from IRQs. */

/* Allocate a buffer for use for formatting a frame to be transmitted.
 * Returns NULL if all buffers are in use. */
cdcncm_buffer_t *cdcncm_allocate();

/* Schedule a buffer for transmission. */
void cdcncm_transmit(cdcncm_buffer_t *buffer);

/* Query current TX buffer size (number of queued frames) */
size_t cdcncm_get_tx_queue_size();

/* Return a received buffer, or NULL. */
cdcncm_buffer_t *cdcncm_receive();

/* Free a received or otherwise allocated buffer. */
void cdcncm_release(cdcncm_buffer_t *buffer);

#endif

