#ifndef TCPIP_H
#define TCPIP_H

#include "network_std.h"
#include "usbnet.h"

#define TCPIP_HEADER_SIZE (14+40+20)
#define TCP_WINDOW_SIZE 16384

extern ipv6_addr_t g_local_ipv6_addr;
extern mac_addr_t g_local_mac_addr;

/* Callback for connection handling. Data will be a pointer to received
 * data, or NULL if this is just a poll call. */
struct _tcpip_conn_t;
typedef void (*tcpip_callback_t)(struct _tcpip_conn_t *conn, usbnet_buffer_t *packet);

typedef enum {
  TCPIP_CLOSED,
  TCPIP_LISTEN,
  TCPIP_ESTABLISHED,
} tcpip_state_t;

typedef struct _tcpip_conn_t {
  struct _tcpip_conn_t *next;
  tcpip_state_t state;
  tcpip_callback_t callback;
  ipv6_addr_t peer_addr;
  mac_addr_t peer_mac;
  uint16_t peer_port;
  uint16_t local_port;
  uint32_t tx_sequence;
  uint32_t rx_sequence;
  uint32_t last_ack_sent;
  void *context; /* Context pointer for use by other modules */
} tcpip_conn_t;

/* Add a new listener. The conn variable will be initialized by this function. */
void tcpip_init_listener(tcpip_conn_t *conn, uint16_t port, tcpip_callback_t callback);

/* Fill in the TCP headers and transmit the packet. The first TCP_HEADER_SIZE bytes
 * will be overwritten. */
void tcpip_send(tcpip_conn_t *conn, usbnet_buffer_t *packet);

/* Close a currently open connection and return it to listeners. */
void tcpip_close(tcpip_conn_t *conn);

/* Polls for new packets from usbnet and calls all callbacks. */
void tcpip_poll();

#endif
