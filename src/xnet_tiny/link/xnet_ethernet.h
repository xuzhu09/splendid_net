//
// Created by efairy520 on 2025/10/21.
//

#ifndef XNET_ETHERNET_H
#define XNET_ETHERNET_H

#include "xnet_def.h"

extern const uint8_t ether_broadcast_mac[];

xnet_status_t ethernet_init(void);
void ethernet_poll(void);
xnet_status_t ethernet_out_to(xnet_protocol_t protocol, const uint8_t *target_mac_addr, xnet_packet_t *packet);

#endif //XNET_ETHERNET_H
