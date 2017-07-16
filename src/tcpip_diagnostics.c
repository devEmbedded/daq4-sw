#include "tcpip_diagnostics.h"
#include "tcpip.h"
#include <string.h>

static tcpip_conn_t g_echo_service;

static void echo_callback(tcpip_conn_t *conn, usbnet_buffer_t *packet)
{
  if (packet)
  {
    tcpip_send(conn, packet);
  }
}

void tcpip_diagnostics_init()
{
  tcpip_init_listener(&g_echo_service, 7, &echo_callback);
}
