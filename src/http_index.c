#include "http_index.h"
#include "http.h"
#include "systime.h"
#include <stdio.h>

void http_index(tcpip_conn_t *conn, http_request_t *request)
{
  char buf[64];
  snprintf(buf, sizeof(buf), "Hello, time is now %u!\n", (unsigned)get_systime());
  
  http_start_response(conn, 200, "text/plain", buf);
  http_end_response(conn);
}

static http_url_handler_t g_index_handler = {NULL, "/", http_index};

void http_index_init()
{
  http_add_url_handler(&g_index_handler);
}
