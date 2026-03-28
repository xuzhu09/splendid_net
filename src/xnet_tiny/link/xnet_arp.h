//
// Created by efairy520 on 2025/10/21.
//

#ifndef XNET_ARP_H
#define XNET_ARP_H

#include "xnet_tiny.h"

void xarp_init(void);
void xarp_poll(void);
void xarp_in(xnet_packet_t *packet);
xnet_status_t xarp_make_request(const xip_addr_t *target_ipaddr);
xnet_status_t xarp_resolve(const xip_addr_t *ipaddr, uint8_t **mac_addr);

#endif //XNET_ARP_H
