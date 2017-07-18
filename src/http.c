#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include "http.h"

#if defined(HTTP_DEBUG) || 1
#define dbg(fmt, ...) printf(__FILE__ ":%3d: " fmt "\n", __LINE__, ##__VA_ARGS__)
#else
#define dbg(fmt, ...)
#endif

#define warn(fmt, ...) printf(__FILE__ ":%3d: [WARN] " fmt "\n", __LINE__, ##__VA_ARGS__)

static tcpip_conn_t g_http_conns[HTTP_MAX_CONNECTIONS];
static http_url_handler_t *g_http_url_handlers;

static void http_dispatch(tcpip_conn_t *conn, http_request_t *request)
{
  http_url_handler_t *handler = g_http_url_handlers;
  while (handler)
  {
    if (strcmp(handler->url, request->url) == 0)
    {
      conn->context = handler->callback;
      handler->callback(conn, request);
      return;
    }
    
    handler = handler->next;
  }
  
  http_start_response(conn, 404, "text/plain", "Not found");
  http_end_response(conn);
}

static bool handle_new_request(tcpip_conn_t *conn, usbnet_buffer_t *packet)
{
  http_request_t request = {};
  char *p = (char*)&packet->data[TCPIP_HEADER_SIZE];
  char *end = (char*)&packet->data[packet->data_size];
  
  /* Parse method */
  while (isspace(*p)) p++;
  if (memcmp(p, "GET ", 4) == 0)
  {
    request.method = HTTP_GET;
    p += 4;
  }
  else if (memcmp(p, "POST ", 5) == 0)
  {
    request.method = HTTP_POST;
    p += 5;
  }
  else
  {
    http_start_response(conn, 400, "text/plain", "Unknown method");
    http_end_response(conn);
    return true;
  }
    
  /* Parse url */
  while (isspace(*p)) p++;
  request.url = p;
  while (p < end && !isspace(*p) && *p != '?') p++;
  if (p >= end) { return false; }
  
  if (*p != '?')
  {
    /* No query string */
    request.query_string = p;
    *p++ = '\0';
  }
  else
  {
    *p++ = '\0';
    request.query_string = p;
  
    /* Parse query string */
    while (p < end && !isspace(*p)) p++;
    if (p >= end) { return false; }
    *p = '\0';
  }
  
  /* Find end of headers */
  while (memcmp(p, "\r\n\r\n", 4) != 0)
  {
    p++;
    while (p < end && *p != '\r') p++;
    if (p >= end) { return false; }
  }
  p += 4;
  request.body_data = (uint8_t*)p;
  request.body_length = (end - p);
  
  dbg("HTTP URL: %s, QS: %s, body_len: %d", request.url, request.query_string, request.body_length);
  http_dispatch(conn, &request);
  return true;
}

static void handle_http_connection(tcpip_conn_t *conn, usbnet_buffer_t *packet)
{
  http_callback_t callback = (http_callback_t)conn->context;
  
  if (conn->state == TCPIP_ESTABLISHED)
  {
    if (!callback)
    {
      if (packet)
      {
        bool status = handle_new_request(conn, packet);
        usbnet_release(packet);
        if (!status)
        {
          warn("HTTP closing after invalid request");
          tcpip_close(conn);
        }
      }
    }
    else
    {
      callback(conn, NULL);
    }
  }
  else
  {
    conn->context = NULL;
  }
}

void http_init()
{
  for (int i = 0; i < HTTP_MAX_CONNECTIONS; i++)
  {
    tcpip_init_listener(&g_http_conns[i], 80, &handle_http_connection);
  }
}

void http_add_url_handler(http_url_handler_t* handler)
{
  handler->next = g_http_url_handlers;
  g_http_url_handlers = handler;
}

void http_start_response(tcpip_conn_t* conn, int status,
                         const char *mime_type, const char* body_data)
{
  dbg("HTTP starting response, status=%d", status);
  
  size_t body_len = strlen(body_data);
  usbnet_buffer_t *packet = usbnet_allocate(256 + body_len);
  if (!packet)
  {
    tcpip_close(conn);
    return;
  }
  
  packet->data_size = TCPIP_HEADER_SIZE;
  packet->data_size += snprintf((char*)&packet->data[packet->data_size],
                               packet->max_size - packet->data_size,
                               "HTTP/1.1 %d %s\r\n"
                               "Content-Type: %s\r\n"
                               "Transfer-Encoding: chunked\r\n"
                               "Connection: keep-alive\r\n"
                               "\r\n",
                              status, (status == 200) ? "OK" : "Error",
                              mime_type);
  
  if (body_len)
  {
    packet->data_size += snprintf((char*)&packet->data[packet->data_size],
                                  packet->max_size - packet->data_size,
                                  "%08x\r\n"
                                  "%s\r\n",
                                  body_len, body_data);
  }
  
  tcpip_send(conn, packet);
}

void http_send_chunk(tcpip_conn_t* conn, usbnet_buffer_t* packet)
{
  assert(packet->data_size >= HTTP_HEADER_SIZE);
  
  size_t body_len = packet->data_size - HTTP_HEADER_SIZE;
  snprintf((char*)&packet->data[TCPIP_HEADER_SIZE], 10, "%08x\r\n", body_len);
  packet->data[packet->data_size++] = '\r';
  packet->data[packet->data_size++] = '\n';
  tcpip_send(conn, packet);
}

void http_end_response(tcpip_conn_t* conn)
{
  usbnet_buffer_t *packet = usbnet_allocate(HTTP_HEADER_SIZE + 2);
  if (!packet)
  {
    tcpip_close(conn);
    return;
  }
  
  packet->data_size = TCPIP_HEADER_SIZE + 5;
  memcpy(&packet->data[TCPIP_HEADER_SIZE], "0\r\n\r\n", 5);
  tcpip_send(conn, packet);
  conn->context = NULL;
}


