//
// Created by efairy520 on 2025/12/5.
//

#ifndef XNET_ICMP_H
#define XNET_ICMP_H

#include "xnet_ip.h"
#include "xnet_def.h"

#define XICMP_TYPE_UNREACH          3           // ICMP不可达类型
#define XICMP_CODE_PORT_UNREACH     3           // 端口不可达
#define XICMP_CODE_PRO_UNREACH      2           // 协议不可达

/**
 * 初始化 ICMP 协议
 */
void xicmp_init(void);

/**
 * 接收 ICMP 请求
 * @param src_ip 来源 ip
 * @param packet 数据包
 */
void xicmp_in(xip_addr_t *src_ip, xnet_packet_t *packet);

/**
 * 异常响应 ICMP 请求
 * @param code
 * @param ip_hdr
 * @return
 */
xnet_status_t xicmp_dest_unreach(uint8_t code, xip_hdr_t *ip_hdr) ;

#endif //XNET_ICMP_H
