#include "tcpip_diagnostics.h"
#include "tcpip.h"
#include <string.h>

static tcpip_conn_t g_echo_service;
static tcpip_conn_t g_discard_service;
static tcpip_conn_t g_chargen_service;

static void echo_callback(tcpip_conn_t *conn, usbnet_buffer_t *packet)
{
  if (packet)
  {
    tcpip_send(conn, packet);
  }
}

static void discard_callback(tcpip_conn_t *conn, usbnet_buffer_t *packet)
{
  if (packet)
  {
    usbnet_release(packet);
  }
}

static void chargen_callback(tcpip_conn_t *conn, usbnet_buffer_t *packet)
{
  static int char_phase = 1;
  static int line_phase = 2;
  static int linepos = 0;
  
  if (conn->state == TCPIP_CLOSED)
  {
    char_phase = 1;
    line_phase = 2;
    linepos = 0;
  }
  
  if (packet)
  {
    usbnet_release(packet);
  }
  
  if (usbnet_get_tx_queue_size() < 2)
  {
    packet = usbnet_allocate(USBNET_BUFFER_SIZE);
    if (packet)
    {
      // RFC 864: generate lines of 72 characters + CRLF
      packet->data_size = TCPIP_HEADER_SIZE;
      while (packet->data_size < packet->max_size)
      {
        linepos++;
        if (linepos <= 72)
        {
          packet->data[packet->data_size++] = ' ' + char_phase++;
          if (char_phase == 95) char_phase = 0;
        }
        else if (linepos == 73)
        {
          packet->data[packet->data_size++] = '\r';
        }
        else
        {
          packet->data[packet->data_size++] = '\n';
          linepos = 0;
          char_phase = line_phase++;
          if (line_phase == 95) line_phase = 0;
        }
      }
      
      tcpip_send(conn, packet);
    }
  }
}

void tcpip_diagnostics_init()
{
  tcpip_init_listener(&g_echo_service, 7, &echo_callback);
  tcpip_init_listener(&g_discard_service, 9, &discard_callback);
  tcpip_init_listener(&g_chargen_service, 19, &chargen_callback);
}
