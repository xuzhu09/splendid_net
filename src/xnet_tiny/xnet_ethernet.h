//
// Created by efairy520 on 2025/10/21.
//

#ifndef XNET_ETHERNET_H
#define XNET_ETHERNET_H

#include "xnet_tiny.h"

typedef enum _xnet_protocol_t {
    XNET_PROTOCOL_ARP = 0x0806, // ARP协议
    XNET_PROTOCOL_IP = 0x0800, // IP协议
} xnet_protocol_t;

xnet_err_t ethernet_init(void);
void ethernet_poll(void);
xnet_err_t xarp_make_request(const xip4_addr_t *target_ipaddr);
xnet_err_t ethernet_out_to(xnet_protocol_t protocol, const uint8_t *mac_addr, xnet_packet_t *packet);
xnet_err_t xarp_make_response(uint8_t *target_ip, uint8_t *target_mac);

#endif //XNET_ETHERNET_H
