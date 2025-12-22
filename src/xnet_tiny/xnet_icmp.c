//
// Created by efairy520 on 2025/12/5.
//

#include "xnet_icmp.h"

#include "xnet_ip.h"
#include <string.h>

void xicmp_init(void) {

}

// 正常响应ICMP请求
static xnet_status_t reply_icmp_request(xicmp_hdr_t* icmp_hdr, xip_addr_t* src_ip, xnet_packet_t* packet) {
    // 构建ICMP回复包
    xnet_packet_t* reply_packet = xnet_alloc_tx_packet(packet->length);

    // 构建ICMP回复包头
    xicmp_hdr_t* reply_hdr = (xicmp_hdr_t*)reply_packet->data;
    reply_hdr->type = XICMP_CODE_ECHO_REPLY;
    reply_hdr->code = 0;
    reply_hdr->id = icmp_hdr->id;
    reply_hdr->seq = icmp_hdr->seq;
    reply_hdr->checksum = 0;

    // 复制载荷（Data）
    memcpy(((uint8_t*)reply_hdr) + sizeof(xicmp_hdr_t),
           ((uint8_t*)icmp_hdr) + sizeof(xicmp_hdr_t),
           packet->length - sizeof(xicmp_hdr_t));

    // 重新计算校验和
    reply_hdr->checksum = checksum16((uint16_t*)reply_hdr, reply_packet->length, 0, 1);

    // 通过 IP 层发送回复包
    return xip_out(XNET_PROTOCOL_ICMP, src_ip, reply_packet);

}

void xicmp_in(xip_addr_t* src_ip, xnet_packet_t* packet) {
    xicmp_hdr_t *icmp_hdr = (xicmp_hdr_t *)packet->data;

    if (packet->length >= sizeof(xicmp_hdr_t) && (icmp_hdr->type == XICMP_CODE_ECHO_REQUEST)) {
        reply_icmp_request(icmp_hdr, src_ip, packet);
    }
}

// 异常响应ICMP请求
xnet_status_t xicmp_dest_unreach(uint8_t code, xip_hdr_t* ip_hdr) {
    xnet_packet_t* packet;
    xicmp_hdr_t* icmp_hdr;
    xip_addr_t dest_ip;

    uint16_t ip_hdr_size = ip_hdr->hdr_len * 4;
    uint16_t ip_data_size = swap_order16(ip_hdr->total_len) - ip_hdr_size;

    // ICMP 错误消息载荷规定：原始 IP 头部 + 原始数据的前 8 字节
    ip_data_size = ip_hdr_size + min(ip_data_size, 8);

    // 分配新的数据包空间：ICMP 头部 + 原始 IP 头部 + 原始数据前 8 字节
    packet = xnet_alloc_tx_packet(sizeof(xicmp_hdr_t) + ip_data_size);

    icmp_hdr = (xicmp_hdr_t*)packet->data;
    icmp_hdr->type = XICMP_TYPE_UNREACH;
    icmp_hdr->code = code; // 使用传入的错误码 (例如 3: Port Unreachable)
    icmp_hdr->id = 0;       // 错误消息不使用 ID 和 SEQ
    icmp_hdr->seq = 0;

    // 复制原始 IP 头部及数据载荷 (从原始 IP 头部开始复制)
    // 目标地址：新 ICMP 头部之后
    // 源地址：导致错误的原始 IP 头部
    memcpy(((uint8_t*)icmp_hdr) + sizeof(xicmp_hdr_t), ip_hdr, ip_data_size);

    icmp_hdr->checksum = 0;
    // 重新计算校验和 (范围是 ICMP 头部 + 原始 IP 头部 + 原始数据)
    icmp_hdr->checksum = checksum16((uint16_t*)icmp_hdr, packet->length, 0, 1);

    // 获取原始包的源 IP 作为新包的目的 IP
    memcpy(&dest_ip, ip_hdr->src_ip, XNET_IPV4_ADDR_SIZE);

    return xip_out(XNET_PROTOCOL_ICMP, &dest_ip, packet);
}

