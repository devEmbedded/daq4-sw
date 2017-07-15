#include <stdio.h>
#include <stddef.h>
#include "board.h"
#include "tcpip.h"
#include "usbnet.h"

#if defined(TCPIP_DEBUG) || 1
#define dbg(fmt, ...) printf("%15s ("__FILE__ ":%3d): " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)
#else
#define dbg(fmt, ...)
#endif

ipv6_addr_t g_ipv6_local_addr = {
  {0xfd, 0xde, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x01}};
  
mac_addr_t g_mac_local_addr = {
  {0x00, 0x11, 0x22, 0x33, 0x44, 0x56}
};

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

static void prepare_multicast_headers(usbnet_buffer_t *packet)
{
  struct {
    ethernet_header_t eth;
    ipv6_header_t ipv6;
  } *response = (void*)packet->data;
  
  response->eth.mac_src = g_mac_local_addr;
  response->eth.mac_dest = MAC_BROADCAST;
  response->eth.ethertype = uint16_to_buint16(ETHERTYPE_IPV6);
  response->ipv6.version_and_class = IPV6_VERSION_CLASS;
  response->ipv6.hop_limit = IPV6_HOP_LIMIT;
  response->ipv6.source = g_ipv6_local_addr;
  response->ipv6.dest = IPV6_ALL_NODES_MULTICAST;
}

static void prepare_reply_headers(usbnet_buffer_t *packet)
{
  struct {
    ethernet_header_t eth;
    ipv6_header_t ipv6;
  } *response = (void*)packet->data;
  
  response->eth.mac_dest = response->eth.mac_src;
  response->eth.mac_src = g_mac_local_addr;
  response->eth.ethertype = uint16_to_buint16(ETHERTYPE_IPV6);
  response->ipv6.version_and_class = IPV6_VERSION_CLASS;
  response->ipv6.hop_limit = IPV6_HOP_LIMIT;
  response->ipv6.dest = response->ipv6.source;
  response->ipv6.source = g_ipv6_local_addr;
}

static void send_neighbor_advertisement(usbnet_buffer_t *packet)
{
  bool solicited = packet;
  ipv6_addr_t target_addr = g_ipv6_local_addr;
  
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
    packet = usbnet_allocate(sizeof(*response));
    if (!packet) return;
    prepare_multicast_headers(packet);
  }
  else
  {
    prepare_reply_headers(packet);
  }
  
  response = (void*)packet->data;
  
  if (solicited && response->payload.target_addr.bytes[0] == 0xfe)
  {
    target_addr = IPV6_LINK_LOCAL_ADDR(g_mac_local_addr);
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
  response->payload.opt.addr = g_mac_local_addr;
  
  response->payload.icmp.checksum = icmp_checksum(&response->ipv6);
  
  packet->data_size = sizeof(*response);
  usbnet_transmit(packet);
  
  dbg("Advertisement sent");
}

static void send_router_advertisement(usbnet_buffer_t *packet)
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
    packet = usbnet_allocate(sizeof(*response));
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
  response->ipv6.source = IPV6_LINK_LOCAL_ADDR(g_mac_local_addr);
  
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
  response->payload.prefix.addr = g_ipv6_local_addr;
  memset(&response->payload.prefix.addr.bytes[8], 0, 8);
  
  response->payload.mtu.type = 5;
  response->payload.mtu.length = 1;
  response->payload.mtu.mtu = uint32_to_buint32(USBNET_BUFFER_SIZE);
  
  response->payload.icmp.checksum = icmp_checksum(&response->ipv6);
  
  packet->data_size = sizeof(*response);
  usbnet_transmit(packet);
  
  dbg("Advertisement sent");
}

static void send_ping_reply(usbnet_buffer_t *packet)
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
  
static void handle_icmp6(usbnet_buffer_t *packet)
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
  else if (hdr->icmp.type == ICMP_TYPE_ECHO_REQUEST)
  {
    send_ping_reply(packet);
  }
  else
  {
    usbnet_release(packet);
  }
}

static void handle_ipv6(usbnet_buffer_t *packet)
{
  struct {
    ethernet_header_t eth;
    ipv6_header_t ipv6;
  } *hdr = (void*)packet->data;
  
  dbg("IPv6: %02x::%02x -> %02x::%02x, nhdr: %d",
      hdr->ipv6.source.bytes[0], hdr->ipv6.source.bytes[15],
      hdr->ipv6.dest.bytes[0], hdr->ipv6.dest.bytes[15],
      hdr->ipv6.next_header);
  
  if (hdr->ipv6.next_header == IP_NEXTHDR_ICMP6)
  {
    handle_icmp6(packet);
  }
  else
  {
    usbnet_release(packet);
  }
}

void tcpip_poll()
{
  if (!usbnet_is_connected())
    return;
  
  usbnet_buffer_t *packet = usbnet_receive();
  
  if (packet)
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
      usbnet_release(packet);
    }
    
    (void)start;
    dbg("---------- Processed, time delta: %10d us\n", (int)(get_systime() - start));
  }
  
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
