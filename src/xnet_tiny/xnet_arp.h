//
// Created by efairy520 on 2025/10/21.
//

#ifndef XNET_ARP_H
#define XNET_ARP_H

#include "xnet_tiny.h"

void xarp_init(void);
void xarp_poll(void);
void update_arp_entry(uint8_t* src_ip, uint8_t* mac_addr);
int xnet_check_tmo(xnet_time_t* last_time, uint32_t gap_time);
xnet_status_t xarp_resolve(const xip_addr_u * ipaddr, uint8_t ** mac_addr);

#endif //XNET_ARP_H
