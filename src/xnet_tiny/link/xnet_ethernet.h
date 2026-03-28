//
// Created by efairy520 on 2025/10/21.
//

#ifndef XNET_ETHERNET_H
#define XNET_ETHERNET_H

#include "xnet_def.h"

extern const uint8_t ether_broadcast_mac[];

// 关闭填充字节
#pragma pack(1)
// 以太网头部 14个字节
typedef struct _xether_hdr_t {
    uint8_t dest[XNET_MAC_ADDR_SIZE];                   // 目标mac地址，6字节
    uint8_t src[XNET_MAC_ADDR_SIZE];                    // 源mac地址，6字节
    uint16_t protocol;                                  // 上层协议，2字节
} xether_hdr_t;
#pragma pack()

xnet_status_t ethernet_init(void);
void ethernet_poll(void);
xnet_status_t ethernet_out_to(xnet_protocol_t protocol, const uint8_t *target_mac_addr, xnet_packet_t *packet);

#endif //XNET_ETHERNET_H
