#include "usbnet.h"
#include "cdcecm_std.h"
#include "rndis_std.h"
#include "usbnet_descriptors.h"
#include "network_std.h"
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <libopencm3/cm3/cortex.h>

#if defined(USBNET_DEBUG)
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

static void usbnet_set_config(usbd_device *usbd_dev, uint16_t wValue);
static int usbnet_ctrl_callback(usbd_device *usbd_dev, struct usb_setup_data *req,
        uint8_t **buf, uint16_t *len, usbd_control_complete_callback *complete);
static void usbnet_altset_callback(usbd_device *usbd_dev, uint16_t wIndex, uint16_t wValue);

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

  usbd_register_set_altsetting_callback(g_usbd_dev, usbnet_altset_callback);
  usbd_register_set_config_callback(g_usbd_dev, usbnet_set_config);

  usbnet_set_config(g_usbd_dev, 0);

  return g_usbd_dev;
}

/**********************
 * USB class protocol *
 **********************/

static void cdcecm_send_connection_status();
static void rndis_send_command(uint8_t *buf, uint16_t len);
static void rndis_get_response(uint8_t **buf, uint16_t *len);
static void rndis_release_response(usbd_device *usbd_dev, struct usb_setup_data *req);
static void cdcecm_status_callback(usbd_device *usbd_dev, uint8_t ep);
static void cdcecm_rx_callback(usbd_device *usbd_dev, uint8_t ep);
static void cdcecm_tx_callback(usbd_device *usbd_dev, uint8_t ep);
static void rndis_rx_callback(usbd_device *usbd_dev, uint8_t ep);
static void rndis_tx_callback(usbd_device *usbd_dev, uint8_t ep);

static int usbnet_ctrl_callback(usbd_device *usbd_dev, struct usb_setup_data *req,
        uint8_t **buf, uint16_t *len, usbd_control_complete_callback *complete)
{
  dbg("Control bmRequestType=%02x, bRequest=%02x, wValue=%04x, wIndex=%04x, wLength=%04x",
      (unsigned)req->bmRequestType,
      (unsigned)req->bRequest, (unsigned)req->wValue, (unsigned)req->wIndex,
      (unsigned)req->wLength);

  if ((req->bmRequestType & USB_REQ_TYPE_TYPE) == USB_REQ_TYPE_CLASS
   && (req->bmRequestType & USB_REQ_TYPE_RECIPIENT) == USB_REQ_TYPE_INTERFACE
   && req->wIndex == RNDIS_INTERFACE)
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
  else if ((req->bmRequestType & USB_REQ_TYPE_TYPE) == USB_REQ_TYPE_STANDARD
        && (req->bmRequestType & USB_REQ_TYPE_RECIPIENT) == USB_REQ_TYPE_DEVICE
        && req->bRequest == USB_REQ_GET_DESCRIPTOR
        && req->wValue == 0x03EE)
  {
    dbg("Microsoft OS descriptor requested");
    static const struct {
      uint8_t bLength;
      uint8_t bDescriptorType;
      uint8_t qwSignature[14];
      uint8_t bMS_VendorCode;
      uint8_t bPad;
    } msft_descr = {0x12, 0x03, {'M',0,'S',0,'F',0,'T',0,'1',0,'0',0,'0',0}, 0xEE, 0};
    *buf = (void*)&msft_descr;
    if (*len > sizeof(msft_descr)) *len = sizeof(msft_descr);
    return USBD_REQ_HANDLED;
  }
  else if (req->bRequest == 0xEE)
  {
    dbg("Microsoft OS function descriptor requested");
    static const struct {
      uint32_t dwLength;
      uint16_t bcdVersion;
      uint16_t wIndex;
      uint8_t bCount;
      uint8_t reserved[7];
      uint8_t bFirstInterfaceNumber;
      uint8_t bInterfaceCount;
      uint8_t compatibleID[8];
      uint8_t subCompatibleID[8];
      uint8_t reserved2[6];
    } msft_descr = {
      .dwLength = 40,
      .bcdVersion = 0x0100,
      .wIndex = 4,
      .bCount = 1,
      .bFirstInterfaceNumber = RNDIS_INTERFACE,
      .bInterfaceCount = 1,
      .compatibleID = "RNDIS",
      .subCompatibleID = "5162001"
    };

    *buf = (void*)&msft_descr;
    if (*len > sizeof(msft_descr)) *len = sizeof(msft_descr);
    return USBD_REQ_HANDLED;
  }

  return USBD_REQ_NEXT_CALLBACK;
}

static void usbnet_altset_callback(usbd_device *usbd_dev, uint16_t wIndex, uint16_t wValue)
{
  printf("Altset %d %d\n", wIndex, wValue);

  if (wValue == 1)
  {
    cdcecm_send_connection_status();
  }
}

static void usbnet_set_config(usbd_device *usbd_dev, uint16_t wValue)
{
  printf("Config %d\n", wValue);

  g_cdcecm_connected = false;
  g_rndis_connected = false;

  usbd_ep_setup(g_usbd_dev, CDCECM_IN_EP, USB_ENDPOINT_ATTR_BULK, 64, cdcecm_tx_callback);
  usbd_ep_setup(g_usbd_dev, CDCECM_OUT_EP, USB_ENDPOINT_ATTR_BULK, 64, cdcecm_rx_callback);
  usbd_ep_setup(g_usbd_dev, CDCECM_IRQ_EP, USB_ENDPOINT_ATTR_INTERRUPT, 16, cdcecm_status_callback);

  usbd_ep_setup(g_usbd_dev, RNDIS_IN_EP, USB_ENDPOINT_ATTR_BULK, 64, rndis_tx_callback);
  usbd_ep_setup(g_usbd_dev, RNDIS_OUT_EP, USB_ENDPOINT_ATTR_BULK, 64, rndis_rx_callback);
  usbd_ep_setup(g_usbd_dev, RNDIS_IRQ_EP, USB_ENDPOINT_ATTR_INTERRUPT, 8, NULL);

  usbd_register_control_callback(g_usbd_dev, 0, 0, usbnet_ctrl_callback);
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

static bool g_cdcecm_send_connection_status;

static void cdcecm_status_callback(usbd_device *usbd_dev, uint8_t ep)
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
    }
}

static void cdcecm_send_connection_status()
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

  usbd_ep_write_packet(g_usbd_dev, CDCECM_IRQ_EP, &notif_speed, sizeof(notif_speed));

  // Interrupt pipe is now full, so send connection status from cdcecm_status_callback.
  g_cdcecm_send_connection_status = true;
}

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
static uint32_t g_rndis_packet_filter;
static uint32_t g_rndis_host_rx_count;
static uint32_t g_rndis_host_tx_count;

static const uint32_t g_rndis_supported_oids[] = {
  RNDIS_OID_GEN_SUPPORTED_LIST,         /* Returns this list */
  RNDIS_OID_GEN_HARDWARE_STATUS,        /* retval = 0 */
  RNDIS_OID_GEN_MEDIA_SUPPORTED,        /* retval = 0 */
  RNDIS_OID_GEN_MEDIA_IN_USE,           /* retval = 0 */
//   RNDIS_OID_GEN_MAXIMUM_LOOKAHEAD,      /* retval = 0 */
  RNDIS_OID_GEN_MAXIMUM_FRAME_SIZE,     /* retval = USBNET_BUFFER_SIZE */
  RNDIS_OID_GEN_LINK_SPEED,             /* retval = 100000 = 10 Mbit/s */
  RNDIS_OID_GEN_TRANSMIT_BLOCK_SIZE,    /* retval = USBNET_BUFFER_SIZE */
  RNDIS_OID_GEN_RECEIVE_BLOCK_SIZE,     /* retval = USBNET_BUFFER_SIZE */
  RNDIS_OID_GEN_VENDOR_ID,              
  RNDIS_OID_GEN_VENDOR_DESCRIPTION,
  RNDIS_OID_GEN_CURRENT_PACKET_FILTER,  /* retval = g_rndis_packet_filter */
  RNDIS_OID_GEN_MAXIMUM_TOTAL_SIZE,     /* retval = 36 + USBNET_BUFFER_SIZE */
//   RNDIS_OID_GEN_MAC_OPTIONS,
  RNDIS_OID_GEN_MEDIA_CONNECT_STATUS,   /* retval = 0 = connected */
  RNDIS_OID_GEN_VENDOR_DRIVER_VERSION,  /* retval = 0 */
  RNDIS_OID_GEN_PHYSICAL_MEDIUM,        /* retval = 0 = ethernet */
  RNDIS_OID_GEN_XMIT_OK,                /* retval = g_rndis_host_tx_count */
  RNDIS_OID_GEN_RCV_OK,                 /* retval = g_rndis_host_rx_count */
  RNDIS_OID_GEN_XMIT_ERROR,             /* retval = 0 */
  RNDIS_OID_GEN_RCV_ERROR,              /* retval = 0 */
  RNDIS_OID_GEN_RCV_NO_BUFFER,          /* retval = 0 */
  RNDIS_OID_802_3_PERMANENT_ADDRESS,    /* retval = g_host_mac_addr */
  RNDIS_OID_802_3_CURRENT_ADDRESS,      /* retval = g_host_mac_addr */
  RNDIS_OID_802_3_MULTICAST_LIST,
  RNDIS_OID_802_3_MAXIMUM_LIST_SIZE,
  RNDIS_OID_802_3_MAC_OPTIONS,          /* retval = 0 */
};

static const struct {
  uint32_t ObjectID;
  uint32_t Length;
  const void *Data;
} g_rndis_oid_values[] = {
  {RNDIS_OID_GEN_SUPPORTED_LIST, sizeof(g_rndis_supported_oids), g_rndis_supported_oids},
  {RNDIS_OID_GEN_MAXIMUM_FRAME_SIZE,    4, &(const uint32_t){USBNET_BUFFER_SIZE}},
  {RNDIS_OID_GEN_LINK_SPEED,            4, &(const uint32_t){100000}},
  {RNDIS_OID_GEN_TRANSMIT_BLOCK_SIZE,   4, &(const uint32_t){USBNET_BUFFER_SIZE}},
  {RNDIS_OID_GEN_RECEIVE_BLOCK_SIZE,    4, &(const uint32_t){USBNET_BUFFER_SIZE}},
  {RNDIS_OID_GEN_VENDOR_ID,             4, &(const uint32_t){0x00FFFFFF}},
  {RNDIS_OID_GEN_VENDOR_DESCRIPTION,    5, "DAQ4"},
  {RNDIS_OID_GEN_CURRENT_PACKET_FILTER, 4, &g_rndis_packet_filter},
  {RNDIS_OID_GEN_MAXIMUM_TOTAL_SIZE,    4, &(const uint32_t){36 + USBNET_BUFFER_SIZE}},
  {RNDIS_OID_GEN_MAC_OPTIONS,           4,
      &(const uint32_t){RNDIS_MAC_OPTION_RECEIVE_SERIALIZED | RNDIS_MAC_OPTION_FULL_DUPLEX}},
  {RNDIS_OID_GEN_XMIT_OK,               4, &g_rndis_host_tx_count},
  {RNDIS_OID_GEN_RCV_OK,                4, &g_rndis_host_rx_count},
  {RNDIS_OID_802_3_PERMANENT_ADDRESS,   6, &g_host_mac_addr},
  {RNDIS_OID_802_3_CURRENT_ADDRESS,     6, &g_host_mac_addr},
  {RNDIS_OID_802_3_MULTICAST_LIST,      4, &(const uint32_t){0xE0000000}},
  {RNDIS_OID_802_3_MAXIMUM_LIST_SIZE,   4, &(const uint32_t){1}},
  {0x0,                                 4, &(const uint32_t){0}}, /* Default fallback */
};

static usbnet_buffer_t *rndis_prepare_response(size_t size, struct rndis_command_header *request_hdr)
{
  usbnet_buffer_t *respbuf = usbnet_allocate(size);
  assert(respbuf);

  respbuf->data_size = size;
  memset(respbuf->data, 0, size);

  struct rndis_response_header *resphdr = (void*)&respbuf->data;
  resphdr->MessageType = request_hdr->MessageType | RNDIS_MSG_COMPLETION;
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
  if (request_hdr->MessageType == RNDIS_MSG_INIT)
  {
    usbnet_buffer_t *respbuf = rndis_prepare_response(sizeof(struct rndis_initialize_cmplt), request_hdr);
    struct rndis_initialize_cmplt *resp = (void*)respbuf->data;

    resp->MajorVersion = 1;
    resp->MinorVersion = 0;
    resp->DeviceFlags = 1;
    resp->Medium = 0;
    resp->MaxPacketsPerTransfer = 1;
    resp->MaxTransferSize = 36 + USBNET_BUFFER_SIZE;
    resp->PacketAlignmentFactor = 2;

    rndis_send_response(respbuf);
  }
  else if (request_hdr->MessageType == RNDIS_MSG_HALT)
  {
    g_rndis_connected = false;
  }
  else if (request_hdr->MessageType == RNDIS_MSG_QUERY)
  {
    usbnet_buffer_t *respbuf = rndis_prepare_response(sizeof(struct rndis_query_cmplt), request_hdr);
    struct rndis_query_msg *req = (void*)buf;
    struct rndis_query_cmplt *resp = (void*)respbuf->data;

    dbg("RNDIS query %08x", (unsigned)req->ObjectID);

    resp->InformationBufferOffset = 16;
    resp->InformationBufferLength = 0;
    resp->hdr.Status = RNDIS_STATUS_NOT_SUPPORTED;
    
    for (int i = 0; i < sizeof(g_rndis_oid_values)/sizeof(g_rndis_oid_values[0]); i++)
    {
      if (g_rndis_oid_values[i].ObjectID == req->ObjectID || g_rndis_oid_values[i].ObjectID == 0)
      {
        resp->hdr.Status = RNDIS_STATUS_SUCCESS;
        resp->InformationBufferLength = g_rndis_oid_values[i].Length;
        memcpy(resp->Buffer, g_rndis_oid_values[i].Data, resp->InformationBufferLength);
        break;
      }
    }

    respbuf->data_size += resp->InformationBufferLength;
    resp->hdr.MessageLength += resp->InformationBufferLength;
    
    rndis_send_response(respbuf);
  }
  else if (request_hdr->MessageType == RNDIS_MSG_SET)
  {
    usbnet_buffer_t *respbuf = rndis_prepare_response(sizeof(struct rndis_response_header), request_hdr);
    struct rndis_set_msg *req = (void*)buf;
    struct rndis_response_header *resp = (void*)respbuf->data;

    dbg("RNDIS set %08x", (unsigned)req->ObjectID);

    if (req->ObjectID == RNDIS_OID_GEN_CURRENT_PACKET_FILTER)
    {
      g_rndis_packet_filter = req->Buffer[0];
      g_rndis_connected = (req->Buffer[0] != 0);

      dbg("RNDIS connection status: %d", (int)g_rndis_connected);
    }
    else if (req->ObjectID == RNDIS_OID_802_3_MULTICAST_LIST)
    {
      dbg("RNDIS multicast list ignored");
    }
    else
    {
      dbg("RNDIS unsupported set %08x", (unsigned)req->ObjectID);
      resp->Status = RNDIS_STATUS_NOT_SUPPORTED;
    }

    rndis_send_response(respbuf);
  }
  else if (request_hdr->MessageType == RNDIS_MSG_RESET)
  {
    usbnet_buffer_t *respbuf = rndis_prepare_response(sizeof(struct rndis_reset_cmplt), request_hdr);
    struct rndis_reset_cmplt *resp = (void*)respbuf->data;
    resp->AddressingReset = 0;
    g_rndis_connected = false;
    rndis_send_response(respbuf);
  }
  else if (request_hdr->MessageType == RNDIS_MSG_KEEPALIVE)
  {
    usbnet_buffer_t *respbuf = rndis_prepare_response(sizeof(struct rndis_response_header), request_hdr);
    rndis_send_response(respbuf);
  }
}

static void rndis_get_response(uint8_t **buf, uint16_t *len)
{
  if (g_rndis_responses)
  {
    dbg("RNDIS response sent");
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

      if (len < 16)
      {
        /* Zero-length-packet termination */
        usbnet_release(g_rndis_current_rx_buffer);
        g_rndis_current_rx_buffer = NULL;
      }
      else if (hdr->MessageType == RNDIS_MSG_PACKET)
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
      g_rndis_host_tx_count++;
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
    hdr.MessageType = RNDIS_MSG_PACKET;
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
      g_rndis_host_rx_count++;
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




