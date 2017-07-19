#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include "http.h"

#define DEBUG
#include "debug.h"

static http_url_handler_t *g_http_url_handlers;

static void http_dispatch(tcpip_conn_t *conn, http_request_t *request)
{
  http_url_handler_t *handler = g_http_url_handlers;
  while (handler)
  {
    if (strcmp(handler->url, request->url) == 0)
    {
      conn->context[TCPIP_CONTEXT_WORDS-1] = (uint32_t)handler->callback;
      handler->callback(conn, request);
      return;
    }
    
    handler = handler->next;
  }
  
  http_start_response(conn, 404, "text/plain", "Not found", true);
}

static bool handle_new_request(tcpip_conn_t *conn, buffer_t *payload)
{
  http_request_t request = {};
  char *p = (char*)(payload->data);
  char *end = (char*)(payload->data + payload->data_size);
  
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
    http_start_response(conn, 400, "text/plain", "Unknown method", true);
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

static void handle_http_connection(tcpip_conn_t *conn, buffer_t *payload)
{
  http_callback_t callback = (http_callback_t)conn->context[TCPIP_CONTEXT_WORDS - 1];
  
  if (conn->state == TCPIP_ESTABLISHED)
  {
    if (!callback)
    {
      if (payload)
      {
        bool status = handle_new_request(conn, payload);
        tcpip_release(payload);
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
}

void http_init()
{
  tcpip_register_listener(80, handle_http_connection);
}

void http_add_url_handler(http_url_handler_t* handler)
{
  handler->next = g_http_url_handlers;
  g_http_url_handlers = handler;
}

void http_start_response(tcpip_conn_t* conn, int status,
                         const char *mime_type, const char* body_data,
                         bool response_done)
{
  dbg("HTTP starting response, status=%d", status);
  
  size_t body_len = strlen(body_data);
  buffer_t *payload = tcpip_allocate(256 + body_len);
  if (!payload)
  {
    warn("HTTP could not allocate buffer for response");
    tcpip_close(conn);
    return;
  }
  
  buffer_printf(payload, "HTTP/1.1 %d %s\r\n"
                         "Content-Type: %s\r\n"
                         "Connection: keep-alive\r\n",
                         status, (status == 200) ? "OK" : "Error", mime_type);
  
  if (body_len)
  {
    if (response_done)
    {
      buffer_printf(payload, "Content-Length: %d\r\n"
                             "\r\n"
                             "%s\r\n",
                    body_len, body_data);
      
      conn->context[TCPIP_CONTEXT_WORDS-1] = 0;
    }
    else
    {
      buffer_printf(payload, "Transfer-Encoding: chunked\r\n"
                             "\r\n"
                             "%08x\r\n"
                             "%s\r\n",
                    body_len, body_data);
    }
  }
  
  tcpip_send(conn, payload);
}

buffer_t *http_allocate_chunk(size_t size)
{
  buffer_t *outer = tcpip_allocate(HTTP_CHUNK_HEADER_SIZE + size + HTTP_CHUNK_TRAILER_SIZE);
  if (outer)
  {
    return buffer_slice(outer, HTTP_CHUNK_HEADER_SIZE, HTTP_CHUNK_TRAILER_SIZE);
  }
  else
  {
    return NULL;
  }
}

void http_release_chunk(buffer_t* chunk)
{
  tcpip_release(buffer_unslice(chunk, HTTP_CHUNK_HEADER_SIZE, HTTP_CHUNK_TRAILER_SIZE));
}

void http_send_chunk(tcpip_conn_t* conn, buffer_t* chunk)
{
  size_t chunklen = chunk->data_size;
  buffer_t *outer = buffer_unslice(chunk, HTTP_CHUNK_HEADER_SIZE, HTTP_CHUNK_TRAILER_SIZE);
  
  snprintf((char*)&outer->data[0], HTTP_CHUNK_HEADER_SIZE, "%08x\r\n", chunklen);
  outer->data[outer->data_size - 2] = '\r';
  outer->data[outer->data_size - 1] = '\n';
  tcpip_send(conn, outer);
}

void http_send_last_chunk(tcpip_conn_t* conn)
{
  buffer_t *payload = tcpip_allocate(5);
  if (!payload)
  {
    tcpip_close(conn);
    return;
  }
  
  payload->data_size = 5;
  memcpy(payload->data, "0\r\n\r\n", 5);
  tcpip_send(conn, payload);
  
  conn->context[TCPIP_CONTEXT_WORDS-1] = 0;
}


