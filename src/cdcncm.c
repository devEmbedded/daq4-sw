#include "cdcncm.h"
#include "cdcncm_std.h"
#include "cdcncm_descriptors.h"
#include <stdlib.h>
#include <assert.h>
#include <libopencm3/cm3/cortex.h>

/******************
 * Initialization *
 ******************/

static cdcncm_buffer_t g_cdcncm_buffers[CDCNCM_BUFFER_COUNT];
static usbd_device *g_usbd_dev;
static uint8_t g_usb_temp_buffer[128];

static void cdcacm_set_config(usbd_device *usbd_dev, uint16_t wValue);

usbd_device *cdcncm_init(const usbd_driver *driver)
{
  
  for (int i = 0; i < CDCNCM_BUFFER_COUNT; i++)
  {
    cdcncm_release(&g_cdcncm_buffers[i]);
  }
  
  g_usbd_dev = usbd_init(driver, &g_device_descriptor, &g_config_descriptor,
        g_usb_strings, CDCNCM_USB_STRING_COUNT,
        g_usb_temp_buffer, sizeof(g_usb_temp_buffer));
  usbd_register_set_config_callback(g_usbd_dev, cdcacm_set_config);
  
  return g_usbd_dev;
}

/**********************
 * USB class protocol *
 **********************/

static bool g_send_ncm_connection_status;
static bool g_cdcncm_connected;
static void cdcncm_rx_callback(usbd_device *usbd_dev, uint8_t ep);
static void cdcncm_tx_callback(usbd_device *usbd_dev, uint8_t ep);

static int cdcncm_ctrl_callback(usbd_device *usbd_dev, struct usb_setup_data *req,
        uint8_t **buf, uint16_t *len, usbd_control_complete_callback *complete)
{
  if (req->bRequest == USB_CDC_REQ_GET_NTB_PARAMETERS)
  {
    static const struct usb_cdc_ncm_ntb_parameters params = {
        .wLength = sizeof(struct usb_cdc_ncm_ntb_parameters),
        .bmNtbFormatsSupported = 1,
        .dwNtbInMaxSize = 4096,
        .wNdpInDivisor = CDCNCM_USB_PACKET_SIZE,
        .wNdpInPayloadRemainder = 14,
        .wNdpInAlignment = 4,
        .dwNtbOutMaxSize = CDCNCM_BUFFER_SIZE + CDCNCM_USB_PACKET_SIZE,
        .wNdpOutDivisor = CDCNCM_USB_PACKET_SIZE,
        .wNdpOutPayloadRemainder = 14,
        .wNdpOutAlignment = 4,
        .wNtbOutMaxDatagrams = 1
    };
    
    *buf = (uint8_t*)&params;
    *len = sizeof(params);
  
    return USBD_REQ_HANDLED;
  }

  return USBD_REQ_NEXT_CALLBACK;
}

static void cdcncm_status_callback(usbd_device *usbd_dev, uint8_t ep)
{
    if (g_send_ncm_connection_status)
    {
      // Tell the host that the link is up
      struct usb_cdc_notification notif_status = {
          .bmRequestType = 0xA1,
          .bNotification = USB_CDC_NOTIFY_NETWORK_CONNECTION,
          .wValue = 1,
          .wIndex = 0,
          .wLength = 0
      };
      usbd_ep_write_packet(usbd_dev, CDCNCM_IRQ_EP, &notif_status, sizeof(notif_status));
      
      g_send_ncm_connection_status = false;
      g_cdcncm_connected = true;
    }
}

static void cdcncm_altset_callback(usbd_device *usbd_dev, uint16_t wIndex, uint16_t wValue)
{
  if (wIndex == 1 && wValue == 1)
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
    
    usbd_ep_write_packet(usbd_dev, CDCNCM_IRQ_EP, &notif_speed, sizeof(notif_speed));

    // Interrupt pipe is now full, so send connection status from callback.
    g_send_ncm_connection_status = true;
  }
}

static void cdcacm_set_config(usbd_device *usbd_dev, uint16_t wValue)
{
  g_cdcncm_connected = false;
  
  usbd_ep_setup(usbd_dev, CDCNCM_IN_EP, USB_ENDPOINT_ATTR_BULK, 64, cdcncm_tx_callback);
  usbd_ep_setup(usbd_dev, CDCNCM_OUT_EP, USB_ENDPOINT_ATTR_BULK, 64, cdcncm_rx_callback);
  usbd_ep_setup(usbd_dev, CDCNCM_IRQ_EP, USB_ENDPOINT_ATTR_INTERRUPT, 16, cdcncm_status_callback);

  usbd_register_control_callback(usbd_dev,
                                 USB_REQ_TYPE_CLASS | USB_REQ_TYPE_INTERFACE,
                                 USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT,
                                 cdcncm_ctrl_callback);
  
  usbd_register_set_altsetting_callback(usbd_dev, cdcncm_altset_callback);
}


/**********************
 * TX/RX linked lists *
 **********************/

static cdcncm_buffer_t *g_cdcncm_freelist;
static cdcncm_buffer_t *g_cdcncm_transmit_queue;
static cdcncm_buffer_t *g_cdcncm_received;

static void list_append(cdcncm_buffer_t **list, cdcncm_buffer_t *element)
{
  assert(element->next == NULL);
  cdcncm_buffer_t **tailptr = list;
  while (*tailptr)
  {
    assert(*tailptr != element);
    tailptr = &(*tailptr)->next;
  }
  *tailptr = element;
}

static cdcncm_buffer_t* list_popfront(cdcncm_buffer_t **list)
{
  if (*list)
  {
    cdcncm_buffer_t *result = *list;
    *list = result->next;
    result->next = NULL;
    return result;
  }
  else
  {
    return NULL;
  }
}

static size_t list_size(cdcncm_buffer_t *list)
{
  size_t size = 0;
  while (list)
  {
    size++;
    list = list->next;
  }
  return size;
}

/*******************
 * RX/TX callbacks *
 *******************/

static size_t g_rx_transfer_size;
static size_t g_rx_frame_offset;
static size_t g_rx_frame_size;
static size_t g_rx_bytes_received;
static bool g_rx_waiting_for_buffer;
static cdcncm_buffer_t *g_current_rx_buffer;

static void cdcncm_rx_callback(usbd_device *usbd_dev, uint8_t ep)
{
  if (g_rx_bytes_received >= g_rx_transfer_size)
  {
    /* Beginning of next transfer */
    size_t len = usbd_ep_read_packet(usbd_dev, ep, g_usb_temp_buffer, CDCNCM_USB_PACKET_SIZE);
    struct usb_cdc_ncm_nth16 *nth16 = (void*)g_usb_temp_buffer;
    
    if (len > 0 && nth16->dwSignature == USB_CDC_NCM_NTH16_SIGNATURE)
    {
      struct usb_cdc_ncm_ndp16 *ndp16 = (void*)(g_usb_temp_buffer + nth16->wNdpIndex);
      g_rx_transfer_size = nth16->wBlockLength;
      g_rx_frame_offset = ndp16->datagrams[0].wDatagramIndex;
      g_rx_frame_size = ndp16->datagrams[0].wDatagramLength;
      g_rx_bytes_received = len;
      g_rx_waiting_for_buffer = false;
      assert(g_rx_frame_size <= CDCNCM_BUFFER_SIZE);
      assert(g_rx_frame_offset % CDCNCM_USB_PACKET_SIZE == 0);
    }
  }
  else if (g_rx_bytes_received >= g_rx_frame_offset &&
      g_rx_bytes_received < g_rx_frame_offset + g_rx_frame_size)
  {
    /* Frame contents */
    if (!g_current_rx_buffer)
    {
      if (list_size(g_cdcncm_received) < CDCNCM_MAX_RX_QUEUE)
      {
        g_current_rx_buffer = cdcncm_allocate();
      }
      
      g_rx_waiting_for_buffer = (g_current_rx_buffer == NULL);
    }
    
    if (g_current_rx_buffer)
    {
      size_t position = g_rx_bytes_received - g_rx_frame_offset;
      size_t max_len = g_rx_transfer_size - g_rx_bytes_received;
      if (max_len > CDCNCM_USB_PACKET_SIZE) max_len = CDCNCM_USB_PACKET_SIZE;
      size_t len = usbd_ep_read_packet(usbd_dev, ep, &g_current_rx_buffer->data[position], max_len);
      
      g_rx_bytes_received += len;
      
      if (position + len >= g_rx_frame_size)
      {
        /* Frame is complete */
        g_current_rx_buffer->data_size = g_rx_frame_size;
        list_append(&g_cdcncm_received, g_current_rx_buffer);
        g_current_rx_buffer = NULL;
      }
    }
  }
  else
  {
    /* Other padding, discard USB packet */
    g_rx_bytes_received += usbd_ep_read_packet(usbd_dev, ep, g_usb_temp_buffer, CDCNCM_USB_PACKET_SIZE);
  }
}

static size_t g_tx_frame_size;
static size_t g_tx_bytes_written;
static bool g_tx_waiting_for_frame = true;
static uint16_t g_tx_sequence_number;
static cdcncm_buffer_t *g_current_tx_buffer;

static void cdcncm_start_tx()
{
  cdcncm_buffer_t *buffer = list_popfront(&g_cdcncm_transmit_queue);
  
  if (buffer)
  {
    g_tx_waiting_for_frame = false;
    g_current_tx_buffer = buffer;
    g_tx_frame_size = buffer->data_size;
    g_tx_bytes_written = 0;
    
    /* This is sized to be exactly 64 bytes */
    struct {
      struct usb_cdc_ncm_nth16 nth16;
      struct usb_cdc_ncm_ndp16_header ndp16;
      struct usb_cdc_ncm_ndp16_pointer datagrams[10];
      struct usb_cdc_ncm_ndp16_pointer terminator;
    } header = {};
    
    header.nth16.dwSignature = USB_CDC_NCM_NTH16_SIGNATURE;
    header.nth16.wHeaderLength = sizeof(header.nth16);
    header.nth16.wSequence = g_tx_sequence_number++;
    header.nth16.wBlockLength = CDCNCM_USB_PACKET_SIZE + g_tx_frame_size;
    header.nth16.wNdpIndex = sizeof(header.nth16);
    
    header.ndp16.dwSignature = USB_CDC_NCM_NDP16_SIGNATURE_NOCRC;
    header.ndp16.wLength = sizeof(header) - sizeof(header.nth16);
    header.datagrams[0].wDatagramIndex = CDCNCM_USB_PACKET_SIZE;
    header.datagrams[0].wDatagramLength = g_tx_frame_size;
    
    size_t len = usbd_ep_write_packet(g_usbd_dev, CDCNCM_IN_EP, &header, CDCNCM_USB_PACKET_SIZE);
    assert(len);
  }
  else
  {
    g_tx_waiting_for_frame = true;
  }
}

static void cdcncm_tx_callback(usbd_device *usbd_dev, uint8_t ep)
{
  if (g_tx_bytes_written < g_tx_frame_size)
  {
    size_t max_len = g_tx_frame_size - g_tx_bytes_written;
    if (max_len > CDCNCM_USB_PACKET_SIZE) max_len = CDCNCM_USB_PACKET_SIZE;
    size_t len = usbd_ep_write_packet(usbd_dev, ep,
      g_current_tx_buffer->data + g_tx_bytes_written, max_len);
    g_tx_bytes_written += len;
    
    if (g_tx_bytes_written >= g_tx_frame_size)
    {
      /* Buffer can be released now */
      cdcncm_release(g_current_tx_buffer);
      g_current_tx_buffer = NULL;
    }
  }
  else
  {
    /* Last packet of frame completed transmission */
    cdcncm_start_tx();
  }
}

/**************
 * Public API *
 **************/

cdcncm_buffer_t *cdcncm_allocate()
{
  CM_ATOMIC_CONTEXT();
  return list_popfront(&g_cdcncm_freelist);
}

void cdcncm_transmit(cdcncm_buffer_t *buffer)
{
  CM_ATOMIC_CONTEXT();
  list_append(&g_cdcncm_transmit_queue, buffer);
  
  if (g_tx_waiting_for_frame)
  {
    cdcncm_start_tx();
  }
}

size_t cdcncm_get_tx_queue_size()
{
  CM_ATOMIC_CONTEXT();
  size_t being_transmitted = (g_tx_waiting_for_frame ? 0 : 1);
  return list_size(g_cdcncm_transmit_queue) + being_transmitted;
}

cdcncm_buffer_t *cdcncm_receive()
{
  CM_ATOMIC_CONTEXT();
  cdcncm_buffer_t *result = list_popfront(&g_cdcncm_received);
  
  if (g_rx_waiting_for_buffer)
  {
    /* Might also be waiting for CDCNCM_MAX_RX_QUEUE */
    cdcncm_rx_callback(g_usbd_dev, CDCNCM_OUT_EP);
  }
  
  return result;
}

void cdcncm_release(cdcncm_buffer_t *buffer)
{
  CM_ATOMIC_CONTEXT();
  list_append(&g_cdcncm_freelist, buffer);
  
  if (g_rx_waiting_for_buffer)
  {
    cdcncm_rx_callback(g_usbd_dev, CDCNCM_OUT_EP);
  }
}




