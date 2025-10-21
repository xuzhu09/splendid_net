//
// Created by efairy520 on 2025/10/21.
//

#ifndef XNET_ETHERNET_H
#define XNET_ETHERNET_H

#include "xnet_tiny.h"

xnet_err_t ethernet_init(void);
void ethernet_poll(void);
xnet_err_t xarp_make_request(const xipaddr_t *target_ipaddr);
xnet_err_t ethernet_out_to(xnet_protocol_t protocol, const uint8_t *mac_addr, xnet_packet_t *packet);

#endif //XNET_ETHERNET_H
