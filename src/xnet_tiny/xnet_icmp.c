//
// Created by efairy520 on 2025/12/5.
//

#include "xnet_icmp.h"
#include "xnet_ip.h"
#include <string.h>

void xicmp_init(void) {

}

static xnet_status_t reply_icmp_request(xicmp_hdr_t* icmp_hdr, xip_addr_t* src_ip, xnet_packet_t* packet) {
    // 答复继续使用
    xnet_packet_t* reply_packet = xnet_alloc_tx_packet(packet->data_length);
    xicmp_hdr_t* reply_hdr = (xicmp_hdr_t*)reply_packet->data_start;

    reply_hdr->type = XICMP_CODE_ECHO_REPLY;
    reply_hdr->code = 0;
    reply_hdr->id = icmp_hdr->id;
    reply_hdr->seq = icmp_hdr->seq;
    reply_hdr->checksum = 0;

    // 复制载荷（Data）
    memcpy(((uint8_t*)reply_hdr) + sizeof(xicmp_hdr_t),
           ((uint8_t*)icmp_hdr) + sizeof(xicmp_hdr_t),
           packet->data_length - sizeof(xicmp_hdr_t));

    // 重新计算校验和
    reply_hdr->checksum = checksum16((uint16_t*)reply_hdr, reply_packet->data_length, 0, 1);

    // 通过 IP 层发送回复包
    return xip_out(XNET_PROTOCOL_ICMP, src_ip, reply_packet);

}

void xicmp_in(xip_addr_t* src_ip, xnet_packet_t* packet) {
    xicmp_hdr_t *icmp_hdr = (xicmp_hdr_t *)packet->data_start;

    if (packet->data_length >= sizeof(xicmp_hdr_t) && (icmp_hdr->type == XICMP_CODE_ECHO_REQUEST)) {
        reply_icmp_request(icmp_hdr, src_ip, packet);
    }
}

