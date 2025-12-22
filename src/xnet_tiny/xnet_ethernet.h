//
// Created by efairy520 on 2025/10/21.
//

#ifndef XNET_ETHERNET_H
#define XNET_ETHERNET_H

#include "xnet_def.h"

// 关闭填充字节
#pragma pack(1)
// 以太网头部 14个字节
typedef struct _xether_hdr_t {
    uint8_t dest[XNET_MAC_ADDR_SIZE]; // 目标mac地址，6字节
    uint8_t src[XNET_MAC_ADDR_SIZE]; // 源mac地址，6字节
    uint16_t protocol; // 协议/长度，2字节
} xether_hdr_t;

typedef struct _xarp_packet_t {
    uint16_t hardware_type, protocol_type;              // 硬件类型、协议类型
    uint8_t hardware_len, protocol_len;                 // 硬件长度、协议长度
    uint16_t opcode;                                    // 请求/响应
    uint8_t sender_mac[XNET_MAC_ADDR_SIZE];             // 发送包硬件地址
    uint8_t sender_ip[XNET_IPV4_ADDR_SIZE];             // 发送包协议地址
    uint8_t target_mac[XNET_MAC_ADDR_SIZE];             // 接收方硬件地址
    uint8_t target_ip[XNET_IPV4_ADDR_SIZE];             // 接收方协议地址
}xarp_packet_t;
#pragma pack()

xnet_status_t ethernet_init(void);
void ethernet_poll(void);
xnet_status_t xarp_make_request(const xip_addr_t *target_ipaddr);
xnet_status_t ethernet_out_to(xnet_protocol_t protocol, const uint8_t *target_mac_addr, xnet_packet_t *packet);
xnet_status_t xarp_make_response(uint8_t *target_ip, uint8_t *target_mac);

#endif //XNET_ETHERNET_H
