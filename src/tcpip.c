#include <stdio.h>
#include <stddef.h>
#include <assert.h>
#include "systime.h"
#include "tcpip.h"
#include "usbnet.h"

/* #define DEBUG */
#include "debug.h"

ipv6_addr_t g_local_ipv6_addr;
mac_addr_t g_local_mac_addr;
tcpip_conn_t g_tcpip_connections[TCPIP_MAX_CONNECTIONS];
tcpip_listener_t g_tcpip_listeners[TCPIP_MAX_LISTENERS];

/************************
 * Checksum calculation *
 ************************/

static uint32_t ipsum(void *data, size_t length)
{
  uint32_t sum = 0;
  uint8_t *bytes = data;
  for (size_t i = 0; i < length; i++)
  {
    sum += (uint32_t)bytes[i] << ((i & 1) ? 0 : 8);
  }
  return sum;
}

static buint16_t foldsum(uint32_t sum)
{
  while (sum >> 16)
  {
    sum = (sum & 0xFFFF) + (sum >> 16);
  }
  
  return uint16_to_buint16(~sum);
}

static buint16_t icmp_checksum(ipv6_header_t *hdr)
{
  ((icmp6_header_t*)(hdr+1))->checksum = uint16_to_buint16(0);
  
  uint32_t sum = 0;
  sum += ipsum(&hdr->source, sizeof(ipv6_addr_t));
  sum += ipsum(&hdr->dest, sizeof(ipv6_addr_t));
  sum += ipsum(&hdr->payload_length, 2);
  sum += hdr->next_header;
  sum += ipsum(hdr + 1, buint16_to_uint16(hdr->payload_length));
  return foldsum(sum);
}

static buint16_t tcp_checksum(ipv6_header_t *hdr)
{
  ((tcp_header_t*)(hdr+1))->checksum = uint16_to_buint16(0);
  
  uint32_t sum = 0;
  sum += ipsum(&hdr->source, sizeof(ipv6_addr_t));
  sum += ipsum(&hdr->dest, sizeof(ipv6_addr_t));
  sum += ipsum(&hdr->payload_length, 2);
  sum += hdr->next_header;
  sum += ipsum(hdr + 1, buint16_to_uint16(hdr->payload_length));
  return foldsum(sum);
}

/*************************
 * IPv6 helper functions *
 *************************/

static bool is_our_address(ipv6_addr_t addr)
{
  if (memcmp(&addr, &g_local_ipv6_addr, sizeof(addr)) == 0)
    return true;
  
  ipv6_addr_t link_addr = IPV6_LINK_LOCAL_ADDR(g_local_mac_addr);
  if (memcmp(&addr, &link_addr, sizeof(addr)) == 0)
    return true;
  
  return false;
}

static void prepare_multicast_headers(buffer_t *packet)
{
  struct {
    ethernet_header_t eth;
    ipv6_header_t ipv6;
  } *response = (void*)packet->data;
  
  response->eth.mac_src = g_local_mac_addr;
  response->eth.mac_dest = MAC_BROADCAST;
  response->eth.ethertype = uint16_to_buint16(ETHERTYPE_IPV6);
  response->ipv6.version_and_class = IPV6_VERSION_CLASS;
  response->ipv6.hop_limit = IPV6_HOP_LIMIT;
  response->ipv6.source = g_local_ipv6_addr;
  response->ipv6.dest = IPV6_ALL_NODES_MULTICAST;
}

static void prepare_reply_headers(buffer_t *packet)
{
  struct {
    ethernet_header_t eth;
    ipv6_header_t ipv6;
  } *response = (void*)packet->data;
  
  response->eth.mac_dest = response->eth.mac_src;
  response->eth.mac_src = g_local_mac_addr;
  response->eth.ethertype = uint16_to_buint16(ETHERTYPE_IPV6);
  response->ipv6.version_and_class = IPV6_VERSION_CLASS;
  response->ipv6.hop_limit = IPV6_HOP_LIMIT;
  response->ipv6.dest = response->ipv6.source;
  response->ipv6.source = g_local_ipv6_addr;
}

/******************************************
 * ICMPv6 neighbour solicitation and ping *
 ******************************************/

static void send_neighbor_advertisement(buffer_t *packet)
{
  bool solicited = (packet != NULL);
  
  struct {
    ethernet_header_t eth;
    ipv6_header_t ipv6;
    struct {
      icmp6_header_t icmp;
      uint32_t flags;
      ipv6_addr_t target_addr;
      icmp6_option_link_address_t opt;
    } __attribute__((packed)) payload;
  } *response;
  
  if (!packet)
  {
    // Unsolicited advert
    packet = buffer_allocate(sizeof(*response));
    if (!packet) return;
    prepare_multicast_headers(packet);
  }
  else
  {
    prepare_reply_headers(packet);
  }
  
  response = (void*)packet->data;
  
  if (solicited && !is_our_address(response->payload.target_addr))
  {
    dbg("Request was not for us.");
    buffer_release(packet);
    return;
  }
  
  ipv6_addr_t target_addr = g_local_ipv6_addr;
  if (solicited && response->payload.target_addr.bytes[0] == 0xfe)
  {
    target_addr = IPV6_LINK_LOCAL_ADDR(g_local_mac_addr);
  }
  
  response->ipv6.payload_length = uint16_to_buint16(sizeof(response->payload));
  response->ipv6.next_header = IP_NEXTHDR_ICMP6;
  memset(&response->payload, 0, sizeof(response->payload));
  response->payload.icmp.type = ICMP_TYPE_NEIGHBOR_ADVERTISEMENT;
  response->payload.icmp.code = 0;
  response->payload.flags = solicited ? 0x60 : 0x20;
  response->payload.target_addr = target_addr;
  response->payload.opt.type = 2;
  response->payload.opt.length = 1;
  response->payload.opt.addr = g_local_mac_addr;
  
  response->payload.icmp.checksum = icmp_checksum(&response->ipv6);
  
  packet->data_size = sizeof(*response);
  usbnet_transmit(packet);
  
  dbg("Neighbour advertisement sent");
}

static void send_router_advertisement(buffer_t *packet)
{
  struct {
    ethernet_header_t eth;
    ipv6_header_t ipv6;
    struct {
      icmp6_header_t icmp;
      uint8_t cur_hop_limit;
      uint8_t flags;
      buint16_t router_lifetime;
      buint32_t reachable_time;
      buint32_t retransmit_timer;
      icmp6_option_prefix_info_t prefix;
      icmp6_option_mtu_t mtu;
    } __attribute__((packed)) payload;
  } *response;
  
  if (!packet)
  {
    // Unsolicited advert
    packet = buffer_allocate(sizeof(*response));
    if (!packet) return;
    prepare_multicast_headers(packet);
  }
  else
  {
    prepare_reply_headers(packet);
  }
  
  response = (void*)packet->data;
  
  response->ipv6.payload_length = uint16_to_buint16(sizeof(response->payload));
  response->ipv6.next_header = IP_NEXTHDR_ICMP6;
  response->ipv6.source = IPV6_LINK_LOCAL_ADDR(g_local_mac_addr);
  
  memset(&response->payload, 0, sizeof(response->payload));
  response->payload.icmp.type = ICMP_TYPE_ROUTER_ADVERTISEMENT;
  response->payload.icmp.code = 0;
  response->payload.cur_hop_limit = 255;
  response->payload.router_lifetime = uint16_to_buint16(3600);
  response->payload.reachable_time = uint32_to_buint32(0xFFFFFFFF);
  response->payload.retransmit_timer = uint32_to_buint32(4000);
  response->payload.prefix.type = 3;
  response->payload.prefix.length = 4;
  response->payload.prefix.prefix_length = 64;
  response->payload.prefix.flags = 0xC0;
  response->payload.prefix.valid_lifetime = uint32_to_buint32(0xFFFFFFFF);
  response->payload.prefix.preferred_lifetime = uint32_to_buint32(0xFFFFFFFF);
  response->payload.prefix.addr = g_local_ipv6_addr;
  memset(&response->payload.prefix.addr.bytes[8], 0, 8);
  
  response->payload.mtu.type = 5;
  response->payload.mtu.length = 1;
  response->payload.mtu.mtu = uint32_to_buint32(USBNET_BUFFER_SIZE);
  
  response->payload.icmp.checksum = icmp_checksum(&response->ipv6);
  
  packet->data_size = sizeof(*response);
  usbnet_transmit(packet);
  
  dbg("Router advertisement sent");
}

static void send_ping_reply(buffer_t *packet)
{
  struct {
    ethernet_header_t eth;
    ipv6_header_t ipv6;
    icmp6_header_t icmp;
    uint16_t identifier;
    uint16_t sequence;
    uint8_t data[];
  } *response = (void*)packet->data;
  
  prepare_reply_headers(packet);
  response->icmp.type = ICMP_TYPE_ECHO_REPLY;
  response->icmp.checksum = icmp_checksum(&response->ipv6);
  
  usbnet_transmit(packet);
  
  dbg("Response sent");
}
  
static void handle_icmp6(buffer_t *packet)
{
  struct {
    ethernet_header_t eth;
    ipv6_header_t ipv6;
    icmp6_header_t icmp;
  } *hdr = (void*)packet->data;
  
  dbg("ICMP: type %d, code 0x%02x",
      hdr->icmp.type, hdr->icmp.code);
  
  if (hdr->icmp.type == ICMP_TYPE_NEIGHBOR_SOLICITATION)
  {
    send_neighbor_advertisement(packet);
  }
  else if (hdr->icmp.type == ICMP_TYPE_ROUTER_SOLICITATION)
  {
    send_router_advertisement(packet);
  }
  else if (hdr->icmp.type == ICMP_TYPE_ECHO_REQUEST && is_our_address(hdr->ipv6.dest))
  {
    send_ping_reply(packet);
  }
  else
  {
    buffer_release(packet);
  }
}

static void icmp6_poll()
{
  systime_t time_now = get_systime();
  int interval = (time_now > 30 * SYSTIME_FREQ) ? 30 * SYSTIME_FREQ : 1 * SYSTIME_FREQ;
  
  static systime_t prev_router_adv;
  if (time_now - prev_router_adv > interval && usbnet_get_tx_queue_size() == 0)
  {
    prev_router_adv = time_now;
    send_router_advertisement(NULL);
  }
  
  static systime_t prev_host_adv;
  if (time_now - prev_host_adv > interval && usbnet_get_tx_queue_size() == 0)
  {
    prev_host_adv = time_now;
    send_neighbor_advertisement(NULL);
  }
}

/**********************
 * TCP/IP connections *
 **********************/

void tcpip_register_listener(uint16_t port, tcpip_callback_t callback)
{
  for (int i = 0; i < TCPIP_MAX_LISTENERS; i++)
  {
    if (g_tcpip_listeners[i].local_port == 0)
    {
      g_tcpip_listeners[i].local_port = port;
      g_tcpip_listeners[i].callback = callback;
      return;
    }
  }
  
  warn("TCP listener slots all in use, not registering port %d", port);
}

void tcpip_send_ctrl(tcpip_conn_t *conn, buffer_t *packet, uint16_t control)
{
  if (packet)
  {
    assert(packet->data_size <= packet->max_size);
    assert(packet->data_size >= TCPIP_HEADER_SIZE);
  }
  else
  {
    packet = buffer_allocate(TCPIP_HEADER_SIZE);
    
    if (!packet)
    {
      dbg("Dropping TCPIP packet due to lack of buffers");
      return;
    }
    
    packet->data_size = TCPIP_HEADER_SIZE;
  }
  
  struct {
    ethernet_header_t eth;
    ipv6_header_t ipv6;
    tcp_header_t tcp;
    buint32_t options[];
  } *hdr = (void*)packet->data;
  
  size_t payload_len = packet->data_size - TCPIP_HEADER_SIZE;
  size_t options_len = 0;
  uint32_t data_offset = 0x5000;
  
  if (control & TCPIP_CONTROL_SYN && payload_len == 0)
  {
    // Send maximum segment size option
    options_len = 4;
    hdr->options[0] = uint32_to_buint32(0x02040000 | (USBNET_BUFFER_SIZE - TCPIP_HEADER_SIZE));
    data_offset = 0x6000;
    packet->data_size += 4;
  }
  
  hdr->eth.ethertype = uint16_to_buint16(ETHERTYPE_IPV6);
  hdr->eth.mac_src = g_local_mac_addr;
  hdr->eth.mac_dest = conn->peer_mac;
  hdr->ipv6.version_and_class = IPV6_VERSION_CLASS;
  hdr->ipv6.payload_length = uint16_to_buint16(sizeof(tcp_header_t) + options_len + payload_len);
  hdr->ipv6.next_header = IP_NEXTHDR_TCP;
  hdr->ipv6.hop_limit = IPV6_HOP_LIMIT;
  hdr->ipv6.source = g_local_ipv6_addr;
  hdr->ipv6.dest = conn->peer_addr;
  hdr->tcp.source_port = uint16_to_buint16(conn->local_port);
  hdr->tcp.dest_port = uint16_to_buint16(conn->peer_port);
  hdr->tcp.sequence = uint32_to_buint32(conn->tx_sequence);
  hdr->tcp.ack = uint32_to_buint32(conn->rx_sequence);
  hdr->tcp.control = uint16_to_buint16(control | data_offset);
  hdr->tcp.window_size = uint16_to_buint16(TCPIP_WINDOW_SIZE);
  hdr->tcp.urgent_pointer = uint16_to_buint16(0);
  hdr->tcp.checksum = tcp_checksum(&hdr->ipv6);
  
  dbg("TCP sending ctrl=%02x len=%d seq=%08x", control,
      (int)payload_len, (unsigned)conn->tx_sequence);
  conn->tx_sequence += payload_len;
  conn->last_ack_sent = conn->rx_sequence;
  conn->last_event = get_systime();
  
  usbnet_transmit(packet);
}

buffer_t* tcpip_allocate(size_t size)
{
  /* We pass the lower layer a smaller buffer and reserve ourselves
   * space for appending the headers later. */
  buffer_t *packet = buffer_allocate(size + TCPIP_HEADER_SIZE);
  if (packet)
  {
    buffer_t *payload = buffer_slice(packet, TCPIP_HEADER_SIZE, 0);
    return payload;
  }
  else
  {
    return NULL;
  }
}

void tcpip_release(buffer_t* buffer)
{
  buffer_release(buffer_unslice(buffer, TCPIP_HEADER_SIZE, 0));
}

void tcpip_send(tcpip_conn_t *conn, buffer_t *payload)
{
  assert(conn->state == TCPIP_ESTABLISHED);
  
  buffer_t *packet = buffer_unslice(payload, TCPIP_HEADER_SIZE, 0);
  tcpip_send_ctrl(conn, packet, TCPIP_CONTROL_ACK);
}

void tcpip_close(tcpip_conn_t *conn)
{
  dbg("TCP closing port=%d", conn->local_port);
  
  tcpip_send_ctrl(conn, NULL, TCPIP_CONTROL_FIN | TCPIP_CONTROL_ACK);
  conn->state = TCPIP_CLOSED;
  conn->callback(conn, NULL);
}

static void tcp_send_rst(buffer_t *packet)
{
  struct {
    ethernet_header_t eth;
    ipv6_header_t ipv6;
    tcp_header_t tcp;
  } *resp = (void*)packet->data;
  
  prepare_reply_headers(packet);
  
  packet->data_size = sizeof(*resp);
  resp->ipv6.payload_length = uint16_to_buint16(sizeof(tcp_header_t));
  buint16_t tmp = resp->tcp.source_port;
  resp->tcp.source_port = resp->tcp.dest_port;
  resp->tcp.dest_port = tmp;
  buint32_t tmp2 = resp->tcp.sequence;
  resp->tcp.sequence = resp->tcp.ack;
  resp->tcp.ack = tmp2;
  
  if (buint16_to_uint16(resp->tcp.control) & TCPIP_CONTROL_SYN)
  {
    resp->tcp.ack = uint32_to_buint32(buint32_to_uint32(resp->tcp.ack) + 1);
  }
  
  resp->tcp.control = uint16_to_buint16(TCPIP_CONTROL_RST | TCPIP_CONTROL_ACK | 0x5000);
  resp->tcp.urgent_pointer = uint16_to_buint16(0);
  resp->tcp.checksum = tcp_checksum(&resp->ipv6);
  
  usbnet_transmit(packet);
}

static tcpip_conn_t *allocate_connection()
{
  tcpip_conn_t *result = NULL;
  systime_t time_now = get_systime();
  int oldest = 0;
  for (int i = 0; i < TCPIP_MAX_CONNECTIONS; i++)
  {
    if (g_tcpip_connections[i].state == TCPIP_CLOSED)
    {
      result = &g_tcpip_connections[i];
      break;
    }
    
    systime_t delta1 = time_now - g_tcpip_connections[oldest].last_event;
    systime_t delta2 = time_now - g_tcpip_connections[i].last_event;
    if (delta2 > delta1)
    {
      oldest = i;
    }
  }
  
  if (!result)
  {
    warn("TCP no free connection slots, terminating oldest connection");
    tcpip_close(&g_tcpip_connections[oldest]);
    result = &g_tcpip_connections[oldest];
  }
  
  memset(result, 0, sizeof(tcpip_conn_t));
  return result;
}

static void handle_tcp_syn(buffer_t *packet)
{
  struct {
    ethernet_header_t eth;
    ipv6_header_t ipv6;
    tcp_header_t tcp;
  } *hdr = (void*)packet->data;
  
  for (int i = 0; i < TCPIP_MAX_LISTENERS; i++)
  {
    if (g_tcpip_listeners[i].local_port == buint16_to_uint16(hdr->tcp.dest_port))
    {
      tcpip_conn_t *conn = allocate_connection();
      conn->state = TCPIP_ESTABLISHED;
      conn->local_port = g_tcpip_listeners[i].local_port;
      conn->callback = g_tcpip_listeners[i].callback;
      
      dbg("TCP connected port=%d", conn->local_port);

      conn->peer_addr = hdr->ipv6.source;
      conn->peer_mac = hdr->eth.mac_src;
      conn->peer_port = buint16_to_uint16(hdr->tcp.source_port);
      conn->rx_sequence = buint32_to_uint32(hdr->tcp.sequence) + 1;
      conn->tx_sequence = conn->rx_sequence + get_systime();
      conn->last_ack_received = conn->tx_sequence;
      packet->data_size = TCPIP_HEADER_SIZE;
      tcpip_send_ctrl(conn, packet, TCPIP_CONTROL_SYN | TCPIP_CONTROL_ACK);
      conn->tx_sequence++;
      
      conn->callback(conn, NULL);
      return;
    }
  }
  
  // No matching listener found, send RST
  warn("TCP no matching listener port=%d", buint16_to_uint16(hdr->tcp.dest_port));
  tcp_send_rst(packet);
}

static void handle_tcp_active(buffer_t *packet)
{
  struct {
    ethernet_header_t eth;
    ipv6_header_t ipv6;
    tcp_header_t tcp;
  } *hdr = (void*)packet->data;
  
  uint16_t control = buint16_to_uint16(hdr->tcp.control);
  uint32_t sequence = buint32_to_uint32(hdr->tcp.sequence);
  uint32_t data_offset = (sizeof(ethernet_header_t) + sizeof(ipv6_header_t) +
                          (buint16_to_uint16(hdr->tcp.control) >> 12) * 4);
  size_t data_len = packet->data_size - data_offset;
  
  for (int i = 0; i < TCPIP_MAX_CONNECTIONS; i++)
  {
    tcpip_conn_t *conn = &g_tcpip_connections[i];
    if (conn->state == TCPIP_ESTABLISHED &&
        conn->local_port == buint16_to_uint16(hdr->tcp.dest_port) &&
        conn->peer_port == buint16_to_uint16(hdr->tcp.source_port) &&
        memcmp(&conn->peer_addr, &hdr->ipv6.source, sizeof(ipv6_addr_t)) == 0)
    {
      conn->last_ack_received = buint32_to_uint32(hdr->tcp.ack);
      conn->last_event = get_systime();
      dbg("TCP data len=%d to port=%d", (int)data_len, conn->local_port);
      
      if (data_len)
      {
        if (sequence < conn->rx_sequence && sequence + TCPIP_WINDOW_SIZE > conn->rx_sequence)
        {
          warn("Ignoring TCP resend");
          buffer_release(packet);
          return;
        }
        else if (sequence > conn->rx_sequence)
        {
          warn("TCPIP sequence mismatch: expected %08x, got %08x",
               (unsigned)conn->rx_sequence, (unsigned)sequence);
          buffer_release(packet);
          tcpip_close(conn);
          return;
        }
        
        conn->rx_sequence += data_len;
        
        if (data_offset > TCPIP_HEADER_SIZE)
        {
          memmove(&packet->data[TCPIP_HEADER_SIZE], &packet->data[data_offset], data_len);
          packet->data_size = TCPIP_HEADER_SIZE + data_len;
        }
        
        buffer_t *payload = buffer_slice(packet, TCPIP_HEADER_SIZE, 0);
        conn->callback(conn, payload);
      }
      else
      {
        buffer_release(packet);
      }
      
      if (control & (TCPIP_CONTROL_FIN | TCPIP_CONTROL_RST))
      {
        conn->rx_sequence++;
        tcpip_close(conn);
      }

      return;
    }
  }
  
  if ((control & TCPIP_CONTROL_ACK) && data_len == 0)
  {
    // Probably just ACK to FIN-ACK, ignore
    buffer_release(packet);
    return;
  }
  
  // No matching connection, reply with RST
  warn("TCP no matching connection port=%d", buint16_to_uint16(hdr->tcp.dest_port));
  tcp_send_rst(packet);
}

// Give all active connections a chance to do their processing
static void tcp_poll()
{
  for (int i = 0; i < TCPIP_MAX_CONNECTIONS; i++)
  {
    tcpip_conn_t *conn = &g_tcpip_connections[i];
  
    if (conn->state == TCPIP_ESTABLISHED)
    {
      conn->callback(conn, NULL);
      
      if (conn->last_ack_sent != conn->rx_sequence)
      {
        // Ack the received data so far
        tcpip_send(conn, NULL);
      }
      
      if (conn->tx_sequence > conn->last_ack_received + 2 * TCPIP_WINDOW_SIZE)
      {
        warn("TCP closing connection due to not getting ACKs");
        tcpip_close(conn);
      }
    }
  }
}

static void handle_tcp(buffer_t *packet)
{
  struct {
    ethernet_header_t eth;
    ipv6_header_t ipv6;
    tcp_header_t tcp;
  } *hdr = (void*)packet->data;
  
  dbg("TCP: dest port=%d, ctrl=%02x",
      buint16_to_uint16(hdr->tcp.dest_port),
      buint16_to_uint16(hdr->tcp.control));
  
  if (buint16_to_uint16(hdr->tcp.control) & TCPIP_CONTROL_SYN)
  {
    handle_tcp_syn(packet);
  }
  else 
  {
    handle_tcp_active(packet);
  }
}

/********************************
 * Polling for received packets *
 ********************************/

static void handle_ipv6(buffer_t *packet)
{
  struct {
    ethernet_header_t eth;
    ipv6_header_t ipv6;
  } *hdr = (void*)packet->data;
  
  size_t packet_size = sizeof(*hdr) + buint16_to_uint16(hdr->ipv6.payload_length);
  if (packet->data_size > packet_size)
  {
    warn("Discarding extra data from packet: %d -> %d",
         (int)packet->data_size, (int)packet_size);
    packet->data_size = packet_size;
  }
  
  dbg("IPv6: %02x::%02x -> %02x::%02x, nhdr: %d",
      hdr->ipv6.source.bytes[0], hdr->ipv6.source.bytes[15],
      hdr->ipv6.dest.bytes[0], hdr->ipv6.dest.bytes[15],
      hdr->ipv6.next_header);
  
  if (hdr->ipv6.next_header == IP_NEXTHDR_ICMP6)
  {
    handle_icmp6(packet);
  }
  else if (hdr->ipv6.next_header == IP_NEXTHDR_TCP)
  {
    handle_tcp(packet);
  }
  else
  {
    buffer_release(packet);
  }
}

void tcpip_poll()
{
  if (!usbnet_is_connected())
    return;
  
  buffer_t *packet = usbnet_receive();
  while (packet)
  {
    ethernet_header_t *hdr = (ethernet_header_t*)packet->data;
    uint16_t ethertype = buint16_to_uint16(hdr->ethertype);
    
    systime_t start = get_systime();
    dbg("----------- Packet received, time: %10d us", (int)start);
    dbg("Ethernet: %3d bytes, :%02x -> :%02x, et: %04x",
        packet->data_size,
        (unsigned)hdr->mac_src.bytes[5],
        (unsigned)hdr->mac_dest.bytes[5],
        ethertype);
    
    if (ethertype == ETHERTYPE_IPV6)
    {
      handle_ipv6(packet);
    }
    else
    {
      buffer_release(packet);
    }
    
    (void)start;
    dbg("---------- Processed, time delta: %10d us\n", (int)(get_systime() - start));
    
    packet = usbnet_receive();
  }
  
  tcp_poll();
  icmp6_poll();
}
