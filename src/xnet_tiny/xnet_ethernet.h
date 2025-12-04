//
// Created by efairy520 on 2025/10/21.
//

#ifndef XNET_ETHERNET_H
#define XNET_ETHERNET_H

#include "xnet_tiny.h"

xnet_status_t ethernet_init(void);
void ethernet_poll(void);
xnet_status_t xarp_make_request(const xip_addr_u *target_ipaddr);
xnet_status_t ethernet_out_to(xnet_protocol_t protocol, const uint8_t *target_mac_addr, xnet_packet_t *packet);
xnet_status_t xarp_make_response(uint8_t *target_ip, uint8_t *target_mac);

#endif //XNET_ETHERNET_H
