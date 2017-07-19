#include "http_index.h"
#include "http.h"
#include "systime.h"
#include <stdio.h>

void http_index(tcpip_conn_t *conn, http_request_t *request)
{
  char buf[64];
  snprintf(buf, sizeof(buf), "Hello, time is now %u!\n", (unsigned)get_systime());
  
  http_start_response(conn, 200, "text/plain", buf, true);
}

void http_firmware_bin(tcpip_conn_t *conn, http_request_t *request)
{
  if (request)
  {
    http_start_response(conn, 200, "application/octet-stream", "", false);
    conn->context[0] = 0x08000000;
  }
  else if (usbnet_get_tx_queue_size() < 2)
  {
    buffer_t *chunk = http_allocate_chunk(HTTP_CHUNK_SIZE);
    if (chunk)
    {
      uint32_t pos = conn->context[0];
      uint32_t end = 0x08000000 + 32768;
      
      size_t max_len = end - pos;
      if (max_len > chunk->max_size)
      {
        max_len = chunk->max_size;
      }
      
      if (max_len > 0)
      {
        memcpy(chunk->data, (void*)pos, max_len);
        chunk->data_size = max_len;
        conn->context[0] = pos + max_len;
        http_send_chunk(conn, chunk);
      }
      else
      {
        http_release_chunk(chunk);
        http_send_last_chunk(conn);
      }
    }
  }
}

static http_url_handler_t g_index_handler = {NULL, "/", http_index};
static http_url_handler_t g_firmware_bin = {NULL, "/api/firmware.bin", http_firmware_bin};

void http_index_init()
{
  http_add_url_handler(&g_index_handler);
  http_add_url_handler(&g_firmware_bin);
}
