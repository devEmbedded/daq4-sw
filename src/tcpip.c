#include <stdio.h>
#include <stddef.h>
#include "board.h"
#include "tcpip.h"
#include "cdcncm.h"

#if 0
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

static systime_t g_prev_router_adv;

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

static void prepare_multicast_headers(cdcncm_buffer_t *packet)
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

static void prepare_reply_headers(cdcncm_buffer_t *packet)
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

static void send_neighbor_advertisement(cdcncm_buffer_t *packet)
{
  struct {
    ethernet_header_t eth;
    ipv6_header_t ipv6;
    struct {
      icmp6_header_t icmp;
      uint32_t flags;
      ipv6_addr_t target_addr;
      icmp6_option_link_address_t opt;
    } __attribute__((packed)) payload;
  } *response = (void*)packet->data;
  
  prepare_reply_headers(packet);
  response->ipv6.payload_length = uint16_to_buint16(sizeof(response->payload));
  response->ipv6.next_header = IP_NEXTHDR_ICMP6;
  
  memset(&response->payload, 0, sizeof(response->payload));
  response->payload.icmp.type = ICMP_TYPE_NEIGHBOR_ADVERTISEMENT;
  response->payload.icmp.code = 0;
  response->payload.flags = 0x60;
  response->payload.target_addr = g_ipv6_local_addr;
  response->payload.opt.type = 2;
  response->payload.opt.length = 1;
  response->payload.opt.addr = g_mac_local_addr;
  
  response->payload.icmp.checksum = icmp_checksum(&response->ipv6);
  
  packet->data_size = sizeof(*response);
  cdcncm_transmit(packet);
  
  dbg("Response sent");
}

static void send_router_advertisement(cdcncm_buffer_t *packet)
{
  if (!packet)
  {
    // Unsolicited advert
    packet = cdcncm_allocate();
    if (!packet) return;
    prepare_multicast_headers(packet);
  }
  else
  {
    prepare_reply_headers(packet);
  }
  
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
  } *response = (void*)packet->data;
  
  response->ipv6.payload_length = uint16_to_buint16(sizeof(response->payload));
  response->ipv6.next_header = IP_NEXTHDR_ICMP6;
  response->ipv6.source = IPV6_LINK_LOCAL_ADDR(g_mac_local_addr);
  
  memset(&response->payload, 0, sizeof(response->payload));
  response->payload.icmp.type = ICMP_TYPE_ROUTER_ADVERTISEMENT;
  response->payload.icmp.code = 0;
  
  response->payload.prefix.type = 3;
  response->payload.prefix.length = 4;
  response->payload.prefix.prefix_length = 64;
  response->payload.prefix.flags = 0x40;
  response->payload.prefix.valid_lifetime = uint32_to_buint32(0xFFFFFFFF);
  response->payload.prefix.preferred_lifetime = uint32_to_buint32(0xFFFFFFFF);
  response->payload.prefix.addr = g_ipv6_local_addr;
  memset(&response->payload.prefix.addr.bytes[8], 0, 8);
  
  response->payload.mtu.type = 5;
  response->payload.mtu.length = 1;
  response->payload.mtu.mtu = uint32_to_buint32(CDCNCM_BUFFER_SIZE);
  
  response->payload.icmp.checksum = icmp_checksum(&response->ipv6);
  
  packet->data_size = sizeof(*response);
  cdcncm_transmit(packet);
  
  dbg("Response sent");
}

static void send_ping_reply(cdcncm_buffer_t *packet)
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
  
  cdcncm_transmit(packet);
  
  dbg("Response sent");
}
  
static void handle_icmp6(cdcncm_buffer_t *packet)
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
    cdcncm_release(packet);
  }
}

static void handle_ipv6(cdcncm_buffer_t *packet)
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
    cdcncm_release(packet);
  }
}

void tcpip_poll()
{
  cdcncm_buffer_t *packet = cdcncm_receive();
  
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
      cdcncm_release(packet);
    }
    
    (void)start;
    dbg("---------- Processed, time delta: %10d us\n", (int)(get_systime() - start));
  }
  
  if (get_systime() - g_prev_router_adv > SYSTIME_FREQ * 2 && cdcncm_get_tx_queue_size() == 0)
  {
    g_prev_router_adv = get_systime();
    send_router_advertisement(NULL);
  }
}
