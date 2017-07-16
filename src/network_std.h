#ifndef NETWORK_STD_H
#define NETWORK_STD_H

#include <stdint.h>

#pragma pack(push,1)

/* Big-endian number types */
typedef struct {
    union {
        uint8_t bytes[2];
        uint16_t word;
    };
} buint16_t;

typedef struct {
    union {
        uint8_t bytes[4];
        uint32_t word;
    };
} buint32_t;

static inline uint16_t buint16_to_uint16(buint16_t x)
{
    return __builtin_bswap16(x.word);
}

static inline uint32_t buint32_to_uint32(buint32_t x)
{
    return __builtin_bswap32(x.word);
}

static inline buint16_t uint16_to_buint16(uint16_t x)
{
    buint16_t result;
    result.word = __builtin_bswap16(x);
    return result;
}

static inline buint32_t uint32_to_buint32(uint32_t x)
{
    buint32_t result;
    result.word = __builtin_bswap32(x);
    return result;
}

/* IEEE 802 48-bit MAC address */
typedef struct {
  uint8_t bytes[6];
} mac_addr_t;

/* IEEE 802.3 Layer 2 Ethernet frame header */
typedef struct {
    mac_addr_t mac_dest;
    mac_addr_t mac_src;
    buint16_t ethertype;
} ethernet_header_t;

/* IPv6 address */
typedef struct {
  uint8_t bytes[16];
} ipv6_addr_t;

/* RFC2460 IPv6 packet header */
typedef struct {
  buint32_t version_and_class;
  buint16_t payload_length;
  uint8_t next_header;
  uint8_t hop_limit;
  ipv6_addr_t source;
  ipv6_addr_t dest;
} ipv6_header_t;

/* TCP packet header */
typedef struct {
    buint16_t source_port;
    buint16_t dest_port;
    buint32_t sequence;
    buint32_t ack;
    buint16_t control;
    buint16_t window_size;
    buint16_t checksum;
    buint16_t urgent_pointer;
} tcp_header_t;

#define TCPIP_CONTROL_ACK 0x0010
#define TCPIP_CONTROL_RST 0x0004
#define TCPIP_CONTROL_SYN 0x0002
#define TCPIP_CONTROL_FIN 0x0001

/* RFC4443 ICMP6 header */
typedef struct {
  uint8_t type;
  uint8_t code;
  buint16_t checksum;
} icmp6_header_t;

typedef struct {
  uint8_t type;
  uint8_t length;
  mac_addr_t addr;
} icmp6_option_link_address_t;

typedef struct {
  uint8_t type;
  uint8_t length;
  uint8_t prefix_length;
  uint8_t flags;
  buint32_t valid_lifetime;
  buint32_t preferred_lifetime;
  uint32_t reserved;
  ipv6_addr_t addr;
} icmp6_option_prefix_info_t;

typedef struct {
  uint8_t type;
  uint8_t length;
  uint16_t reserved;
  buint32_t mtu;
} icmp6_option_mtu_t;

#define ETHERTYPE_IPV4 0x0800
#define ETHERTYPE_IPV6 0x86dd

#define IPV6_VERSION_CLASS (buint32_t){{{0x60, 0x00, 0x00, 0x00}}}
#define IPV6_HOP_LIMIT 255

#define IPV6_NULL_ADDRESS (ipv6_addr_t){{0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0}}
#define IPV6_ALL_NODES_MULTICAST (ipv6_addr_t){{0xFF,0x02,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,1}}
#define MAC_NULL (mac_addr_t){{0, 0, 0, 0, 0, 0}}
#define MAC_BROADCAST (mac_addr_t){{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}}

#define IPV6_LINK_LOCAL_ADDR(mac) \
  (ipv6_addr_t){{0xFE,0x80,0,0, 0,0,0,0, 0,0,mac.bytes[0],mac.bytes[1], mac.bytes[2],mac.bytes[3],mac.bytes[4],mac.bytes[5]}}

#define IP_NEXTHDR_ICMP6 58
#define IP_NEXTHDR_TCP 6

#define ICMP_TYPE_UNREACHABLE               1
#define ICMP_TYPE_ECHO_REQUEST            128
#define ICMP_TYPE_ECHO_REPLY              129
#define ICMP_TYPE_ROUTER_SOLICITATION     133
#define ICMP_TYPE_ROUTER_ADVERTISEMENT    134
#define ICMP_TYPE_NEIGHBOR_SOLICITATION   135
#define ICMP_TYPE_NEIGHBOR_ADVERTISEMENT  136

#define ICMP_CODE_PORT_UNREACHABLE        4

#pragma pack(pop)

#endif