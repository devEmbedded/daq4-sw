#include "tcpip_diagnostics.h"
#include "tcpip.h"
#include <string.h>

static void echo_callback(tcpip_conn_t *conn, buffer_t *payload)
{
  if (payload)
  {
    tcpip_send(conn, payload);
  }
}

static void discard_callback(tcpip_conn_t *conn, buffer_t *payload)
{
  if (payload)
  {
    tcpip_release(payload);
  }
}

static void chargen_callback(tcpip_conn_t *conn, buffer_t *payload)
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
  
  if (payload)
  {
    tcpip_release(payload);
  }
  
  if (usbnet_get_tx_queue_size() < 2)
  {
    payload = tcpip_allocate(TCPIP_MAX_PAYLOAD);
    if (payload)
    {
      // RFC 864: generate lines of 72 characters + CRLF
      payload->data_size = 0;
      while (payload->data_size < payload->max_size)
      {
        linepos++;
        if (linepos <= 72)
        {
          payload->data[payload->data_size++] = ' ' + char_phase++;
          if (char_phase == 95) char_phase = 0;
        }
        else if (linepos == 73)
        {
          payload->data[payload->data_size++] = '\r';
        }
        else
        {
          payload->data[payload->data_size++] = '\n';
          linepos = 0;
          char_phase = line_phase++;
          if (line_phase == 95) line_phase = 0;
        }
      }
      
      tcpip_send(conn, payload);
    }
  }
}

void tcpip_diagnostics_init()
{
  tcpip_register_listener(7, echo_callback);
  tcpip_register_listener(9, discard_callback);
  tcpip_register_listener(19, chargen_callback);
}

