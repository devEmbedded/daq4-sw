#ifndef TCPIP_H
#define TCPIP_H

#include "network_std.h"
#include "usbnet.h"
#include "systime.h"

#define TCPIP_HEADER_SIZE (14+40+20)
#define TCPIP_MAX_PAYLOAD (USBNET_BUFFER_SIZE - TCPIP_HEADER_SIZE)
#define TCPIP_WINDOW_SIZE 16384
#define TCPIP_MAX_CONNECTIONS 4
#define TCPIP_MAX_LISTENERS 8
#define TCPIP_CONTEXT_WORDS 8

extern ipv6_addr_t g_local_ipv6_addr;
extern mac_addr_t g_local_mac_addr;

/* Callback for connection handling. Data will be a pointer to received
 * data, or NULL if this is just a poll call. */
struct _tcpip_conn_t;
typedef void (*tcpip_callback_t)(struct _tcpip_conn_t *conn, buffer_t *payload);

typedef enum {
  TCPIP_CLOSED = 0,
  TCPIP_ESTABLISHED,
} tcpip_state_t;

typedef struct _tcpip_conn_t {
  tcpip_state_t state;
  tcpip_callback_t callback;
  ipv6_addr_t peer_addr;
  mac_addr_t peer_mac;
  uint16_t peer_port;
  uint16_t local_port;
  uint32_t tx_sequence;
  uint32_t rx_sequence;
  uint32_t last_ack_sent;
  uint32_t last_ack_received;
  systime_t last_event;
  
  /* Place for other modules to store per-connection data. */
  uint32_t context[TCPIP_CONTEXT_WORDS];
} tcpip_conn_t;

typedef struct {
  uint16_t local_port;
  tcpip_callback_t callback;
} tcpip_listener_t;

/* Register TCP listener for a port */
void tcpip_register_listener(uint16_t port, tcpip_callback_t callback);

/* Allocate a buffer that can later be given to tcpip_send().
 * Safe to call from IRQs.
 */
buffer_t *tcpip_allocate(size_t size);

/* Release a received or otherwise allocated tcpip buffer. */
void tcpip_release(buffer_t *buffer);

/* Fill in the TCP headers and transmit the payload.
 * Buffer must have been allocated using tcpip_allocate().
 */
void tcpip_send(tcpip_conn_t *conn, buffer_t *payload);

/* Close a currently open connection and return it to listeners. */
void tcpip_close(tcpip_conn_t *conn);

/* Polls for new packets from usbnet and calls all callbacks. */
void tcpip_poll();

#endif
