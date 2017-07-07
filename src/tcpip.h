#ifndef TCPIP_H
#define TCPIP_H

#include "network_std.h"

extern ipv6_addr_t g_ipv6_local_addr;
extern mac_addr_t g_mac_local_addr;

void tcpip_poll();

#endif
