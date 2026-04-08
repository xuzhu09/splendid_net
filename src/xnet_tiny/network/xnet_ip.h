//
// Created by efairy520 on 2025/10/26.
//

#ifndef XNET_IP_H
#define XNET_IP_H

#include "xnet_tiny.h"

#define XNET_VERSION_IPV4                   4   // IPV4

// 提取版本号 (取高 4 位)
#define XIP_VERSION(hdr)        (((hdr)->version_ihl >> 4) & 0x0F)

// 提取头部长度 (取低 4 位)
#define XIP_HDR_LEN(hdr)        ((hdr)->version_ihl & 0x0F)

// 设置版本号和头部长度
#define XIP_SET_VERSION_IHL(hdr, ver, len) \
((hdr)->version_ihl = (((ver) & 0x0F) << 4) | ((len) & 0x0F))

#pragma pack(1)
// IP头部，20个字节
typedef struct _xip_hdr_t {
    uint8_t version_ihl;                        // 版本号 (高4位) 和 头部长度 (低4位) 合并为一个字节
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

/**
 * 初始化 IP 协议
 */
void xip_init(void);

/**
 * 收到一个 IP 包
 * @param packet
 */
void xip_in(xnet_packet_t *packet);

/**
 * 发送一个 IP 包
 * @param protocol
 * @param dest_ip
 * @param packet
 * @return
 */
xnet_status_t xip_out(xnet_protocol_t protocol, xip_addr_t *dest_ip, xnet_packet_t *packet);

/**
 * 校验和计算
 * @param buf 校验数据区的起始地址
 * @param len 数据区的长度，以字节为单位
 * @param pre_sum 累加的之前的值，用于多次调用checksum对不同的的数据区计算出一个校验和
 * @param complement 是否对累加和的结果进行取反
 * @return 校验和结果
 */
uint16_t checksum16(uint16_t *buf, uint16_t len, uint16_t pre_sum, int complement);

/**
 * 计算带有伪头部的校验和
 *
 * @param src_ip
 * @param dest_ip
 * @param protocol
 * @param buf
 * @param len
 * @return
 */
uint16_t pseudo_checksum(const xip_addr_t *src_ip, const xip_addr_t *dest_ip, uint8_t protocol,
                       uint16_t *buf, uint16_t len);

#endif //XNET_IP_H
