//
// Created by efairy520 on 2025/10/26.
//

#ifndef XNET_IP_H
#define XNET_IP_H

#include "xnet_tiny.h"

#define XNET_VERSION_IPV4                   4   // IPV4
#define XNET_IP_DEFAULT_TTL                 64  // 缺省的IP包TTL值

#pragma pack(1)
typedef struct _xip_hdr_t {
    uint8_t hdr_len : 4;                        // 头部长度(4字节为单位),低四位
    uint8_t version : 4;                        // 版本号，高四位
    uint8_t tos;		                        // 服务类型，type of service
    uint16_t total_len;		                    // 总长度
    uint16_t id;		                        // 标识符
    uint16_t flags_fragment;                    // 标志与分段，暂时不使用，填0
    uint8_t ttl;                                // 存活时间
    uint8_t protocol;	                        // 上层协议
    uint16_t hdr_checksum;                      // 首部校验和
    uint8_t	src_ip[XNET_IPV4_ADDR_SIZE];        // 源IP
    uint8_t dest_ip[XNET_IPV4_ADDR_SIZE];	    // 目标IP
}xip_hdr_t;
#pragma pack()

void xip_init(void);
void xip_in(xnet_packet_t* packet);
xnet_status_t xip_out(xnet_protocol_t protocol, xip_addr_u* dest_ip, xnet_packet_t * packet);

#endif //XNET_IP_H
