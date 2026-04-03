//
// Created by efairy520 on 2025/12/5.
//

#include "xnet_icmp.h"

#include "xnet_ip.h"
#include <string.h>

#define XICMP_TYPE_ECHO_REQUEST     8           // ICMP请求类型
#define XICMP_TYPE_ECHO_REPLY       0           // ICMP响应类型

#pragma pack(1)
typedef struct _xicmp_hdr_t {
    uint8_t type;       // 类型：8=Request, 0=Reply
    uint8_t code;       // 代码：0
    uint16_t checksum;  // 校验和
    uint16_t id;        // 标识符，执行ping的进程id
    uint16_t seq;       // 序列号，发送的ping请求序号，1,2,3,4
    // 后面紧接着就是 Data，用指针访问即可
} xicmp_hdr_t;
#pragma pack()

void xicmp_init(void) {

}

/**
 * 响应 ICMP 请求
 * @param icmp_hdr
 * @param src_ip
 * @param packet
 * @return
 */
static xnet_status_t reply_icmp_request(xicmp_hdr_t *icmp_hdr, xip_addr_t *src_ip, xnet_packet_t *packet) {
    // 构建ICMP回复包
    xnet_packet_t *reply_packet = xnet_alloc_tx_packet(packet->len);

    // 构建ICMP回复包头
    xicmp_hdr_t *reply_hdr = (xicmp_hdr_t*)reply_packet->data;
    reply_hdr->type = XICMP_TYPE_ECHO_REPLY;
    reply_hdr->code = 0;
    reply_hdr->id = icmp_hdr->id;
    reply_hdr->seq = icmp_hdr->seq;
    reply_hdr->checksum = 0;

    // 复制载荷（Data）
    memcpy(((uint8_t*)reply_hdr) + sizeof(xicmp_hdr_t),
           ((uint8_t*)icmp_hdr) + sizeof(xicmp_hdr_t),
           packet->len - sizeof(xicmp_hdr_t));

    // 重新计算校验和
    reply_hdr->checksum = checksum16((uint16_t*)reply_hdr, reply_packet->len, 0, 1);

    // 通过 IP 层发送回复包
    return xip_out(XNET_PROTOCOL_ICMP, src_ip, reply_packet);

}

void xicmp_in(xip_addr_t *src_ip, xnet_packet_t *packet) {
    xicmp_hdr_t *icmp_hdr = (xicmp_hdr_t *)packet->data;

    if (packet->len >= sizeof(xicmp_hdr_t) && (icmp_hdr->type == XICMP_TYPE_ECHO_REQUEST)) {
        reply_icmp_request(icmp_hdr, src_ip, packet);
    }
}


xnet_status_t xicmp_dest_unreach(uint8_t code, xip_hdr_t *ip_hdr) {
    // 1. 计算ICMP载荷大小：原始IP头部 + 原始IP载荷的前8字节
    uint16_t ip_hdr_size = XIP_HDR_LEN(ip_hdr) * 4;
    uint16_t ip_payload_size = swap_order16(ip_hdr->total_len) - ip_hdr_size;
    uint16_t icmp_payload_size = ip_hdr_size + min(ip_payload_size, 8);

    // 2. 分配新的发送包：ICMP 头部 + ICMP载荷
    xnet_packet_t *tx_packet = xnet_alloc_tx_packet(sizeof(xicmp_hdr_t) + icmp_payload_size);

    // 3. 设置ICMP头部信息
    xicmp_hdr_t *icmp_hdr = (xicmp_hdr_t*)tx_packet->data;
    icmp_hdr->type = XICMP_TYPE_UNREACH;
    icmp_hdr->code = code;  // 详细类型 (例如 3: Port Unreachable)
    icmp_hdr->id = 0;       // 错误消息不使用 ID 和 SEQ
    icmp_hdr->seq = 0;

    // 4. 设置ICMP载荷信息（从源ip_hdr头部开始，复制icmp_payload_size长度的数据到icmp_hdr的载荷区）
    memcpy(((uint8_t*)icmp_hdr) + sizeof(xicmp_hdr_t), ip_hdr, icmp_payload_size);

    // 5. 计算ICMP包校验和
    icmp_hdr->checksum = 0;
    icmp_hdr->checksum = checksum16((uint16_t*)icmp_hdr, tx_packet->len, 0, 1);

    // 6. 从源IP头部提取src_ip作为dest_ip
    xip_addr_t dest_ip;
    memcpy(&dest_ip, ip_hdr->src_ip, XNET_IPV4_ADDR_SIZE);

    return xip_out(XNET_PROTOCOL_ICMP, &dest_ip, tx_packet);
}

