#include "usbnet.h"
#include "cdcecm_std.h"
#include "rndis_std.h"
#include "usbnet_descriptors.h"
#include "network_std.h"
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <libopencm3/cm3/cortex.h>

#if defined(USBNET_DEBUG) || 1
#define dbg(fmt, ...) printf("%15s ("__FILE__ ":%3d): " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)
#else
#define dbg(fmt, ...)
#endif

/******************
 * Initialization *
 ******************/

/* There are a few small buffers for ACK packets and such,
 * and a few large buffers for data transfers. */
static struct {usbnet_buffer_t buf; uint8_t data[USBNET_BUFFER_SIZE];} g_usbnet_bigbuffers[USBNET_BUFFER_COUNT];
static struct {usbnet_buffer_t buf; uint8_t data[USBNET_SMALLBUF_SIZE];} g_usbnet_smallbuffers[USBNET_SMALLBUF_COUNT];
static usbnet_buffer_t *g_usbnet_free_small;
static usbnet_buffer_t *g_usbnet_free_big;

static usbd_device *g_usbd_dev;
static uint8_t g_usb_temp_buffer[512] __attribute__((aligned(4)));

static mac_addr_t g_host_mac_addr = {{0x00, 0x11, 0x22, 0x33, 0x44, 0x55}};

static bool g_cdcecm_connected;
static bool g_rndis_connected;
static usbnet_buffer_t *g_usbnet_transmit_queue;
static usbnet_buffer_t *g_usbnet_received;

static void cdcacm_set_config(usbd_device *usbd_dev, uint16_t wValue);

usbd_device *usbnet_init(const usbd_driver *driver)
{
  
  for (int i = 0; i < USBNET_BUFFER_COUNT; i++)
  {
    *(uint16_t*)&g_usbnet_bigbuffers[i].buf.max_size = USBNET_BUFFER_SIZE;
    usbnet_release(&g_usbnet_bigbuffers[i].buf);
  }
  
  for (int i = 0; i < USBNET_SMALLBUF_COUNT; i++)
  {
    *(uint16_t*)&g_usbnet_smallbuffers[i].buf.max_size = USBNET_SMALLBUF_SIZE;
    usbnet_release(&g_usbnet_smallbuffers[i].buf);
  }
  
  g_usbd_dev = usbd_init(driver, &g_device_descriptor, &g_config_descriptor,
        g_usb_strings, USBNET_USB_STRING_COUNT,
        g_usb_temp_buffer, sizeof(g_usb_temp_buffer));
  usbd_register_set_config_callback(g_usbd_dev, cdcacm_set_config);
  
  return g_usbd_dev;
}

/**********************
 * USB class protocol *
 **********************/

static bool g_cdcecm_send_connection_status;
static void cdcecm_rx_callback(usbd_device *usbd_dev, uint8_t ep);
static void cdcecm_tx_callback(usbd_device *usbd_dev, uint8_t ep);
static void rndis_rx_callback(usbd_device *usbd_dev, uint8_t ep);
static void rndis_tx_callback(usbd_device *usbd_dev, uint8_t ep);
static void rndis_send_command(uint8_t *buf, uint16_t len);
static void rndis_get_response(uint8_t **buf, uint16_t *len);
static void rndis_release_response(usbd_device *usbd_dev, struct usb_setup_data *req);
static void cdcecm_start_tx();

static int usbnet_ctrl_callback(usbd_device *usbd_dev, struct usb_setup_data *req,
        uint8_t **buf, uint16_t *len, usbd_control_complete_callback *complete)
{
  if (req->wIndex == RNDIS_INTERFACE)
  {
    if (req->bRequest == RNDIS_SEND_ENCAPSULATED_COMMAND)
    {
      rndis_send_command(*buf, *len);
      return USBD_REQ_HANDLED;
    }
    else if (req->bRequest == RNDIS_GET_ENCAPSULATED_RESPONSE)
    {
      rndis_get_response(buf, len);
      *complete = rndis_release_response;
      return USBD_REQ_HANDLED;
    }
  }
  
  return USBD_REQ_NEXT_CALLBACK;
}

static void usbnet_status_callback(usbd_device *usbd_dev, uint8_t ep)
{
    if (g_cdcecm_send_connection_status)
    {
      // Tell the host that the link is up
      struct usb_cdc_notification notif_status = {
          .bmRequestType = 0xA1,
          .bNotification = USB_CDC_NOTIFY_NETWORK_CONNECTION,
          .wValue = 1,
          .wIndex = 0,
          .wLength = 0
      };
      usbd_ep_write_packet(usbd_dev, CDCECM_IRQ_EP, &notif_status, sizeof(notif_status));
      
      g_cdcecm_send_connection_status = false;
      g_cdcecm_connected = true;
      dbg("CDCECM connection status: %d\n", (int)g_cdcecm_connected);
      
      cdcecm_start_tx();
    }
}

static void usbnet_altset_callback(usbd_device *usbd_dev, uint16_t wIndex, uint16_t wValue)
{
  printf("Altset %d %d\n", wIndex, wValue);
  
  if (wValue == 1)
  {
    // Inform host about connection speed (required message in CDC specs)
    struct usb_cdc_ncm_connection_speed notif_speed = {
        .cdc_header = {
            .bmRequestType = 0xA1,
            .bNotification = USB_CDC_NOTIFY_CONNECTION_SPEED_CHANGE,
            .wValue = 1,
            .wIndex = 0,
            .wLength = 8
          },
          .DLBitRate = 12000000,
          .ULBitRate = 12000000
    };
    
    usbd_ep_write_packet(usbd_dev, CDCECM_IRQ_EP, &notif_speed, sizeof(notif_speed));

    // Interrupt pipe is now full, so send connection status from callback.
    g_cdcecm_send_connection_status = true;
  }
}

static void cdcacm_set_config(usbd_device *usbd_dev, uint16_t wValue)
{
  printf("Config %d\n", wValue);
  
  g_cdcecm_connected = false;
  g_rndis_connected = false;
  
  usbd_ep_setup(usbd_dev, CDCECM_IN_EP, USB_ENDPOINT_ATTR_BULK, 64, cdcecm_tx_callback);
  usbd_ep_setup(usbd_dev, CDCECM_OUT_EP, USB_ENDPOINT_ATTR_BULK, 64, cdcecm_rx_callback);
  usbd_ep_setup(usbd_dev, CDCECM_IRQ_EP, USB_ENDPOINT_ATTR_INTERRUPT, 16, usbnet_status_callback);

  usbd_ep_setup(usbd_dev, RNDIS_IN_EP, USB_ENDPOINT_ATTR_BULK, 64, rndis_tx_callback);
  usbd_ep_setup(usbd_dev, RNDIS_OUT_EP, USB_ENDPOINT_ATTR_BULK, 64, rndis_rx_callback);
  usbd_ep_setup(usbd_dev, RNDIS_IRQ_EP, USB_ENDPOINT_ATTR_INTERRUPT, 8, NULL);
  
  usbd_register_control_callback(usbd_dev,
                                 USB_REQ_TYPE_CLASS | USB_REQ_TYPE_INTERFACE,
                                 USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT,
                                 usbnet_ctrl_callback);
  
  usbd_register_set_altsetting_callback(usbd_dev, usbnet_altset_callback);
  
  
}


/**********************
 * TX/RX linked lists *
 **********************/

static void list_append(usbnet_buffer_t **list, usbnet_buffer_t *element)
{
  assert(element->next == NULL);
  usbnet_buffer_t **tailptr = list;
  while (*tailptr)
  {
    assert(*tailptr != element);
    tailptr = &(*tailptr)->next;
  }
  *tailptr = element;
}

static usbnet_buffer_t* list_popfront(usbnet_buffer_t **list)
{
  if (*list)
  {
    usbnet_buffer_t *result = *list;
    *list = result->next;
    result->next = NULL;
    return result;
  }
  else
  {
    return NULL;
  }
}

static size_t list_size(usbnet_buffer_t *list)
{
  size_t size = 0;
  while (list)
  {
    size++;
    list = list->next;
  }
  return size;
}

/***************************
 * CDC ECM RX/TX callbacks *
 ***************************/

static size_t g_cdcecm_rx_bytes_received;
static bool g_cdcecm_rx_waiting_for_buffer;
static usbnet_buffer_t *g_cdcecm_current_rx_buffer;

static void cdcecm_rx_callback(usbd_device *usbd_dev, uint8_t ep)
{
  if (!g_cdcecm_current_rx_buffer)
  {
    /* Beginning of next transfer */
    if (list_size(g_usbnet_received) < USBNET_MAX_RX_QUEUE)
    {
      g_cdcecm_current_rx_buffer = usbnet_allocate(USBNET_BUFFER_SIZE);
    }
    
    g_cdcecm_rx_bytes_received = 0;
    g_cdcecm_rx_waiting_for_buffer = (g_cdcecm_current_rx_buffer == NULL);
  }
  
  if (g_cdcecm_current_rx_buffer)
  {
    if (g_cdcecm_rx_bytes_received >= g_cdcecm_current_rx_buffer->max_size)
    {
      size_t len = usbd_ep_read_packet(usbd_dev, ep, &g_usb_temp_buffer,
                                     USBNET_USB_PACKET_SIZE);
      g_cdcecm_rx_bytes_received += len;
      if (len < USBNET_USB_PACKET_SIZE)
      {
        printf("Discarding too long packet: %d\n", g_cdcecm_rx_bytes_received);
        usbnet_release(g_cdcecm_current_rx_buffer);
        g_cdcecm_current_rx_buffer = NULL;
      }
    }
    else
    {    
      size_t len = usbd_ep_read_packet(usbd_dev, ep, &g_cdcecm_current_rx_buffer->data[g_cdcecm_rx_bytes_received],
                                      USBNET_USB_PACKET_SIZE);
      g_cdcecm_rx_bytes_received += len;
      
      if (len < USBNET_USB_PACKET_SIZE)
      {
        /* Transfer complete */
        g_cdcecm_current_rx_buffer->data_size = g_cdcecm_rx_bytes_received;
        list_append(&g_usbnet_received, g_cdcecm_current_rx_buffer);
        g_cdcecm_current_rx_buffer = NULL;
      }
    }
  }
}

static size_t g_cdcecm_tx_bytes_written;
static bool g_cdcecm_tx_waiting_for_frame = true;
static usbnet_buffer_t *g_cdcecm_current_tx_buffer;

static void cdcecm_start_tx()
{
  usbnet_buffer_t *buffer = list_popfront(&g_usbnet_transmit_queue);
  
  if (buffer)
  {
    g_cdcecm_tx_waiting_for_frame = false;
    g_cdcecm_current_tx_buffer = buffer;
    g_cdcecm_tx_bytes_written = 0;
    
    cdcecm_tx_callback(g_usbd_dev, CDCECM_IN_EP);
  }
  else
  {
    g_cdcecm_tx_waiting_for_frame = true;
  }
}

static void cdcecm_tx_callback(usbd_device *usbd_dev, uint8_t ep)
{
  if (g_cdcecm_current_tx_buffer)
  {
    size_t max_len = g_cdcecm_current_tx_buffer->data_size - g_cdcecm_tx_bytes_written;
    if (max_len > USBNET_USB_PACKET_SIZE) max_len = USBNET_USB_PACKET_SIZE;
    size_t len = usbd_ep_write_packet(usbd_dev, ep,
      &g_cdcecm_current_tx_buffer->data[g_cdcecm_tx_bytes_written], max_len);
    g_cdcecm_tx_bytes_written += len;
    
    if (len < USBNET_USB_PACKET_SIZE)
    {
      /* Buffer can be released now */
      usbnet_release(g_cdcecm_current_tx_buffer);
      g_cdcecm_current_tx_buffer = NULL;
    }
  }
  else
  {
    /* Last packet of frame completed transmission */
    cdcecm_start_tx();
  }
}

/*******************
 * RNDIS callbacks *
 *******************/

static usbnet_buffer_t *g_rndis_responses;

static usbnet_buffer_t *rndis_prepare_response(size_t size, struct rndis_command_header *request_hdr)
{
  usbnet_buffer_t *respbuf = usbnet_allocate(size);
  assert(respbuf);
  
  respbuf->data_size = size;
  memset(respbuf->data, 0, size);
  
  struct rndis_response_header *resphdr = (void*)&respbuf->data;
  resphdr->MessageType = request_hdr->MessageType | 0x80000000;
  resphdr->MessageLength = size;
  resphdr->RequestID = request_hdr->RequestID;
  resphdr->Status = RNDIS_STATUS_SUCCESS;

  return respbuf;
}

static void rndis_send_response(usbnet_buffer_t *response)
{
  list_append(&g_rndis_responses, response);
  
  struct rndis_notification notif = {RNDIS_NOTIFICATION_RESPONSE_AVAILABLE, 0};
  usbd_ep_write_packet(g_usbd_dev, RNDIS_IRQ_EP, &notif, sizeof(notif));
}

static void rndis_send_command(uint8_t *buf, uint16_t len)
{
  struct rndis_command_header *request_hdr = (void*)buf;
  
  dbg("RNDIS request %02x", (unsigned)request_hdr->MessageType);
  if (request_hdr->MessageType == RNDIS_INITIALIZE_MSG)
  {
    usbnet_buffer_t *respbuf = rndis_prepare_response(sizeof(struct rndis_initialize_cmplt), request_hdr);
    struct rndis_initialize_cmplt *resp = (void*)respbuf->data;
    
    resp->MajorVersion = 1;
    resp->MinorVersion = 0;
    resp->DeviceFlags = 0x10;
    resp->Medium = 0;
    resp->MaxPacketsPerTransfer = 1;
    resp->MaxTransferSize = USBNET_BUFFER_SIZE;
    resp->PacketAlignmentFactor = 2;
    
    rndis_send_response(respbuf);
  }
  else if (request_hdr->MessageType == RNDIS_HALT_MSG)
  {
    g_rndis_connected = false;
  }
  else if (request_hdr->MessageType == RNDIS_QUERY_MSG)
  {
    usbnet_buffer_t *respbuf = rndis_prepare_response(sizeof(struct rndis_query_cmplt), request_hdr);
    struct rndis_query_msg *req = (void*)buf;
    struct rndis_query_cmplt *resp = (void*)respbuf->data;
    
    dbg("RNDIS query %08x", (unsigned)req->ObjectID);
    
    if (req->ObjectID == RNDIS_OID_SUPPORTED_LIST)
    {
      static const uint32_t supported_oids[] = {
        RNDIS_OID_SUPPORTED_LIST, RNDIS_OID_PHYSICAL_MEDIUM,
        RNDIS_OID_PERMANENT_ADDRESS, RNDIS_OID_CURRENT_ADDRESS
      };
      memcpy(resp->Buffer, supported_oids, sizeof(supported_oids));
      resp->InformationBufferLength = sizeof(supported_oids);
    }
    else if (req->ObjectID == RNDIS_OID_PHYSICAL_MEDIUM)
    {
      resp->InformationBufferLength = sizeof(uint32_t);
      resp->Buffer[0] = 0;
    }
    else if (req->ObjectID == RNDIS_OID_PACKET_FILTER)
    {
      resp->InformationBufferLength = sizeof(uint32_t);
      resp->Buffer[0] = (g_rndis_connected ? 0xFFFFFFFF : 0);
    }
    else if (req->ObjectID == RNDIS_OID_PERMANENT_ADDRESS
          || req->ObjectID == RNDIS_OID_CURRENT_ADDRESS)
    {
      resp->InformationBufferLength = sizeof(g_host_mac_addr);
      memcpy(resp->Buffer, &g_host_mac_addr, sizeof(g_host_mac_addr));
    }
    else
    {
      dbg("RNDIS unsupported query %08x", (unsigned)req->ObjectID);
      resp->hdr.Status = RNDIS_STATUS_NOT_SUPPORTED;
    }
    
    if (resp->InformationBufferLength)
    {
      respbuf->data_size += resp->InformationBufferLength;
      resp->hdr.MessageLength += resp->InformationBufferLength;
      resp->InformationBufferOffset = 16;
    }
    
    rndis_send_response(respbuf);
  }
  else if (request_hdr->MessageType == RNDIS_SET_MSG)
  {
    usbnet_buffer_t *respbuf = rndis_prepare_response(sizeof(struct rndis_response_header), request_hdr);
    struct rndis_set_msg *req = (void*)buf;
    struct rndis_response_header *resp = (void*)respbuf->data;
    
    dbg("RNDIS set %08x\n", (unsigned)req->ObjectID);
    
    if (req->ObjectID == RNDIS_OID_PACKET_FILTER)
    {
      g_rndis_connected = (req->Buffer[0] != 0);
      
      dbg("RNDIS connection status: %d\n", (int)g_rndis_connected);
    }
    else
    {
      dbg("RNDIS unsupported set %08x", (unsigned)req->ObjectID);
      resp->Status = RNDIS_STATUS_NOT_SUPPORTED;
    }
    
    rndis_send_response(respbuf);
  }
}

static void rndis_get_response(uint8_t **buf, uint16_t *len)
{
  if (g_rndis_responses)
  {
    dbg("RNDIS response");
    *len = g_rndis_responses->data_size;
    *buf = g_rndis_responses->data;
  }
  else
  {
    *len = 0;
  }
}

static void rndis_release_response(usbd_device *usbd_dev, struct usb_setup_data *req)
{
  if (g_rndis_responses)
  {
    usbnet_release(list_popfront(&g_rndis_responses));
  }
}

static size_t g_rndis_rx_bytes_received;
static size_t g_rndis_rx_transfer_size;
static size_t g_rndis_rx_frame_offset;
static size_t g_rndis_rx_frame_size;
static bool g_rndis_rx_waiting_for_buffer;
static usbnet_buffer_t *g_rndis_current_rx_buffer;

static void rndis_rx_callback(usbd_device *usbd_dev, uint8_t ep)
{
  if (g_rndis_rx_bytes_received >= g_rndis_rx_transfer_size)
  {
    /* Start of new transfer */
    if (!g_rndis_current_rx_buffer)
    {
      if (list_size(g_usbnet_received) < USBNET_MAX_RX_QUEUE)
      {
        g_rndis_current_rx_buffer = usbnet_allocate(USBNET_BUFFER_SIZE);
      }
      
      g_rndis_rx_waiting_for_buffer = (g_rndis_current_rx_buffer == NULL);
    }
    
    if (g_rndis_current_rx_buffer)
    {
      size_t len = usbd_ep_read_packet(usbd_dev, ep, g_usb_temp_buffer, USBNET_USB_PACKET_SIZE);
      struct rndis_packet_msg *hdr = (void*)g_usb_temp_buffer;
      
      if (len >= 16 && hdr->MessageType == RNDIS_PACKET_MSG)
      {
        g_rndis_rx_bytes_received = len;
        g_rndis_rx_transfer_size = hdr->MessageLength;
        g_rndis_rx_frame_offset = hdr->DataOffset + 8;
        g_rndis_rx_frame_size = hdr->DataLength;
        
        if (g_rndis_rx_frame_offset < len)
        {
          /* Copy the part of packet that is in first buffer already */
          memcpy(g_rndis_current_rx_buffer->data, &g_usb_temp_buffer[g_rndis_rx_frame_offset],
                 len - g_rndis_rx_frame_offset);
        }
      }
      else
      {
        dbg("RNDIS unknown message %08x", (unsigned)hdr->MessageType);
        usbnet_release(g_rndis_current_rx_buffer);
        g_rndis_current_rx_buffer = NULL;
      }
    }
  }
  else if (g_rndis_rx_bytes_received >= g_rndis_rx_frame_offset &&
           g_rndis_rx_bytes_received < g_rndis_rx_frame_offset + g_rndis_rx_frame_size)
  {
    /* Frame contents */
    size_t position = g_rndis_rx_bytes_received - g_rndis_rx_frame_offset;
    size_t max_len = g_rndis_rx_transfer_size - g_rndis_rx_bytes_received;
    if (max_len > USBNET_USB_PACKET_SIZE) max_len = USBNET_USB_PACKET_SIZE;
    size_t len = usbd_ep_read_packet(usbd_dev, ep, &g_rndis_current_rx_buffer->data[position], max_len);
    
    g_rndis_rx_bytes_received += len;
    
    if (position + len >= g_rndis_rx_frame_size)
    {
      /* Frame is complete */
      g_rndis_current_rx_buffer->data_size = g_rndis_rx_frame_size;
      list_append(&g_usbnet_received, g_rndis_current_rx_buffer);
      g_rndis_current_rx_buffer = NULL;
    }
  }
  else
  {
    /* Other padding, discard USB packet */
    g_rndis_rx_bytes_received += usbd_ep_read_packet(usbd_dev, ep, g_usb_temp_buffer, USBNET_USB_PACKET_SIZE);
  }
}

static size_t g_rndis_tx_frame_size;
static size_t g_rndis_tx_bytes_written;
static bool g_rndis_tx_waiting_for_frame = true;
static usbnet_buffer_t *g_rndis_current_tx_buffer;

static void rndis_start_tx()
{
  usbnet_buffer_t *buffer = list_popfront(&g_usbnet_transmit_queue);
  
  if (buffer)
  {
    g_rndis_tx_waiting_for_frame = false;
    g_rndis_current_tx_buffer = buffer;
    g_rndis_tx_frame_size = buffer->data_size;
    g_rndis_tx_bytes_written = 0;
    
    /* This is sized to be exactly 64 bytes */
    struct rndis_packet_msg hdr = {};
    hdr.MessageType = RNDIS_PACKET_MSG;
    hdr.MessageLength = sizeof(hdr) + buffer->data_size;
    hdr.DataOffset = sizeof(hdr) - 8;
    hdr.DataLength = buffer->data_size;
    
    size_t len = usbd_ep_write_packet(g_usbd_dev, RNDIS_IN_EP, &hdr, USBNET_USB_PACKET_SIZE);
    assert(len);
  }
  else
  {
    g_rndis_tx_waiting_for_frame = true;
  }
}

static void rndis_tx_callback(usbd_device *usbd_dev, uint8_t ep)
{
  if (g_rndis_tx_bytes_written < g_rndis_tx_frame_size)
  {
    size_t max_len = g_rndis_tx_frame_size - g_rndis_tx_bytes_written;
    if (max_len > USBNET_USB_PACKET_SIZE) max_len = USBNET_USB_PACKET_SIZE;
    size_t len = usbd_ep_write_packet(usbd_dev, ep,
      g_rndis_current_tx_buffer->data + g_rndis_tx_bytes_written, max_len);
    g_rndis_tx_bytes_written += len;
    
    if (g_rndis_tx_bytes_written >= g_rndis_tx_frame_size)
    {
      /* Buffer can be released now */
      usbnet_release(g_rndis_current_tx_buffer);
      g_rndis_current_tx_buffer = NULL;
    }
  }
  else
  {
    /* Last packet of frame completed transmission */
    rndis_start_tx();
  }
}

/**************
 * Public API *
 **************/

bool usbnet_is_connected()
{
  CM_ATOMIC_CONTEXT();
  return g_cdcecm_connected || g_rndis_connected;
}

usbnet_buffer_t *usbnet_allocate(size_t size)
{
  CM_ATOMIC_CONTEXT();
  
  usbnet_buffer_t *result = NULL;
  
  if (size <= USBNET_SMALLBUF_SIZE)
  {
    result = list_popfront(&g_usbnet_free_small);
  }
  
  if (!result)
  {
    result = list_popfront(&g_usbnet_free_big);
  }
  
  if (result)
  {
    assert(result->max_size >= size);
  }
  else
  {
    dbg("No buffers left, trying to allocate %d bytes", (int)size);
  }
  
  return result;
}

void usbnet_transmit(usbnet_buffer_t *buffer)
{
  CM_ATOMIC_CONTEXT();
  list_append(&g_usbnet_transmit_queue, buffer);
  
  if (g_cdcecm_tx_waiting_for_frame && g_cdcecm_connected)
  {
    cdcecm_start_tx();
  }
  
  if (g_rndis_tx_waiting_for_frame && g_rndis_connected)
  {
    rndis_start_tx();
  }
}

size_t usbnet_get_tx_queue_size()
{
  CM_ATOMIC_CONTEXT();
  size_t being_transmitted = (g_cdcecm_tx_waiting_for_frame ? 0 : 1);
  return list_size(g_usbnet_transmit_queue) + being_transmitted;
}

usbnet_buffer_t *usbnet_receive()
{
  CM_ATOMIC_CONTEXT();
  usbnet_buffer_t *result = list_popfront(&g_usbnet_received);
  
  if (g_cdcecm_rx_waiting_for_buffer)
  {
    /* Might be waiting for USBNET_MAX_RX_QUEUE */
    cdcecm_rx_callback(g_usbd_dev, CDCECM_OUT_EP);
  }
  
  if (g_rndis_rx_waiting_for_buffer)
  {
    /* Might be waiting for USBNET_MAX_RX_QUEUE */
    rndis_rx_callback(g_usbd_dev, RNDIS_OUT_EP);
  }
  
  return result;
}

void usbnet_release(usbnet_buffer_t *buffer)
{
  CM_ATOMIC_CONTEXT();
  
  if (buffer->max_size >= USBNET_BUFFER_SIZE)
  {
    list_append(&g_usbnet_free_big, buffer);
  }
  else
  {
    list_append(&g_usbnet_free_small, buffer);
  }
  
  if (g_cdcecm_rx_waiting_for_buffer)
  {
    cdcecm_rx_callback(g_usbd_dev, CDCECM_OUT_EP);
  }
  
  if (g_rndis_rx_waiting_for_buffer)
  {
    rndis_rx_callback(g_usbd_dev, RNDIS_OUT_EP);
  }
}




