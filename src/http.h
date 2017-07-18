#ifndef HTTP_H
#define HTTP_H

#include "tcpip.h"

#define HTTP_MAX_CONNECTIONS 4
#define HTTP_HEADER_SIZE (TCPIP_HEADER_SIZE+10)
#define HTTP_CHUNK_SIZE (USBNET_BUFFER_SIZE-HTTP_HEADER_SIZE-2)

typedef enum {
  HTTP_GET,
  HTTP_POST
} http_method_t;

typedef struct {
  http_method_t method;
  const char *url;
  const char *query_string;
  size_t body_length;
  uint8_t *body_data;
} http_request_t;

/* Callback for request handling. The first call for a new request will have
 * the request parameter non-NULL. The callback should then call http_start_response().
 * It can either finish in a single call by calling http_end_response(), or it can return
 * and receive periodic callbacks with request = NULL and send body data with
 * http_send_chunk().
 */
typedef void (*http_callback_t)(tcpip_conn_t *conn, http_request_t *request);

typedef struct _http_url_handler_t {
  struct _http_url_handler_t *next;
  const char *url; /* E.g. "/api/version" */
  http_callback_t callback;
} http_url_handler_t;

/* Start HTTP listeners */
void http_init();

void http_add_url_handler(http_url_handler_t *handler);

/* Start sending a response to client. Status should be e.g. 200 or 404. */
void http_start_response(tcpip_conn_t *conn, int status,
                         const char *mime_type, const char *body_data);

/* Send response body chunk */
void http_send_chunk(tcpip_conn_t *conn, usbnet_buffer_t *packet);

/* Send response end chunk */
void http_end_response(tcpip_conn_t *conn);

#endif
