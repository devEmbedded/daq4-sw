#ifndef TCPIP_H
#define TCPIP_H

#include "network_std.h"

extern ipv6_addr_t g_local_ipv6_addr;
extern mac_addr_t g_local_mac_addr;

void tcpip_poll();

#endif
