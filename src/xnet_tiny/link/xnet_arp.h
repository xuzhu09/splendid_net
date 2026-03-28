//
// Created by efairy520 on 2025/10/21.
//

#ifndef XNET_ARP_H
#define XNET_ARP_H

#include "xnet_tiny.h"

// 宏定义也移过来
#define XARP_HW_ETHER               0x1                 // 以太网
#define XARP_REQUEST                0x1                 // ARP请求包
#define XARP_REPLY                  0x2                 // ARP响应包

// 关闭填充字节
#pragma pack(1)
typedef struct _xarp_packet_t {
    uint16_t hardware_type, protocol_type;              // 硬件类型、协议类型
    uint8_t hardware_len, protocol_len;                 // 硬件长度、协议长度
    uint16_t opcode;                                    // 请求/响应
    uint8_t sender_mac[XNET_MAC_ADDR_SIZE];             // 发送包硬件地址
    uint8_t sender_ip[XNET_IPV4_ADDR_SIZE];             // 发送包协议地址
    uint8_t target_mac[XNET_MAC_ADDR_SIZE];             // 接收方硬件地址
    uint8_t target_ip[XNET_IPV4_ADDR_SIZE];             // 接收方协议地址
} xarp_packet_t;
#pragma pack()


void xarp_init(void);
void xarp_poll(void);
void xarp_in(xnet_packet_t *packet);
xnet_status_t xarp_make_request(const xip_addr_t *target_ipaddr);
xnet_status_t xarp_resolve(const xip_addr_t *ipaddr, uint8_t **mac_addr);

#endif //XNET_ARP_H
