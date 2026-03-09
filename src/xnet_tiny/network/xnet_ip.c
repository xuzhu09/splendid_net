//
// Created by efairy520 on 2025/10/26.
//
#include "xnet_ip.h"

#include <stdio.h>
#include <string.h>
#include "xnet_arp.h"
#include "xnet_ethernet.h"
#include "xnet_icmp.h"
#include "xnet_tcp.h"
#include "xnet_udp.h"

uint16_t checksum16(uint16_t *buf, uint16_t len, uint16_t pre_sum, int complement) {
    // 使用32位接收16位，因为要处理溢出
    uint32_t checksum = pre_sum;
    uint16_t high;

    // 每次取出两个字节，累加
    while (len > 1) {
        checksum += *buf++;
        len -= 2;
    }
    // 剩余一个，单独处理
    if (len > 0) {
        checksum += *(uint8_t *)buf;
    }

    // 如果sum长度超过16位，不断进行高16+低16，直至结果小于16位
    while ((high = checksum >> 16) != 0) {
        checksum = high + (checksum & 0xffff);
    }
    // 传入的是1，就对sum取反
    return complement ? (uint16_t)~checksum : (uint16_t)checksum;
}

uint16_t checksum_peso(const xip_addr_t *src_ip, const xip_addr_t *dest_ip, uint8_t protocol,
                      uint16_t *buf, uint16_t len) {

    // 1. 伪头部要求：协议号前需填充 1 字节 0，以构成一个完整的 16 位字。
    uint8_t zero_protocol[] = { 0, protocol };

    // 2. 伪头部要求：传输层数据段的长度（len）必须以网络字节序参与校验和计算。
    uint16_t c_len = swap_order16(len);

    // 3. 初始化校验和累加器 sum，计算源 IP 地址 (4字节) 的 16 位求和。
    uint32_t sum = checksum16((uint16_t*)src_ip->addr, XNET_IPV4_ADDR_SIZE, 0, 0);

    // 累加目的 IP 地址 (4字节) 的 16 位求和。
    sum = checksum16((uint16_t*)dest_ip->addr, XNET_IPV4_ADDR_SIZE, sum, 0);

    // 累加 (0 + 协议号) 字段 (2字节) 的 16 位求和。
    sum = checksum16((uint16_t*)zero_protocol, 2, sum, 0);

    // 累加长度字段 c_len (2字节) 的 16 位求和。
    sum = checksum16((uint16_t*)&c_len, 2, sum, 0);

    // 4. 最终计算
    // 累加传输层数据段 (buf 和 len) 的校验和到 sum 中。
    // 最后一个参数 '1' 表示这是最后一次调用，checksum16 内部将完成最终的反码运算和溢出处理，返回最终的 16 位校验和结果。
    return checksum16(buf, len, sum, 1);
}

void xip_init(void) {

}

void xip_in(xnet_packet_t *packet) {
    xip_hdr_t *ip_hdr = (xip_hdr_t*) packet->data;

    // 进行一些必要性的检查：版本号要求
    if (XIP_VERSION(ip_hdr) != XNET_VERSION_IPV4) {
        return;
    }

    // 长度要求检查
    uint32_t ip_hdr_len = XIP_HDR_LEN(ip_hdr) * 4;
    uint32_t ip_total_len = swap_order16(ip_hdr->total_len);
    if ((ip_hdr_len < sizeof(xip_hdr_t)) || ((ip_total_len < ip_hdr_len) || (packet->len < ip_total_len))) {
        return;
    }

    // 校验和要求检查
    uint16_t pre_checksum = ip_hdr->hdr_checksum; //取出原校验和
    ip_hdr->hdr_checksum = 0; //校验和本身也会参与运算，先归零
    if (pre_checksum != checksum16((uint16_t*)ip_hdr, ip_hdr_len, 0, 1)) {
        return;
    }
    ip_hdr->hdr_checksum = pre_checksum; //恢复校验和

    // 无论是 TCP、UDP 还是 ICMP，都以 IP 头部的 total_len 为准
    // 如果驱动给的数据比 total_len 长（说明有以太网Padding），直接切掉
    if (packet->len > ip_total_len) {
        truncate_packet(packet, ip_total_len);
    }

    // 只处理目标IP为自己的数据包，其它广播之类的IP全部丢掉
    if (!xip_addr_eq(xnet_local_ip.addr, ip_hdr->dest_ip)) {
        return;
    }
    xip_addr_t src_ip;
    memcpy(src_ip.addr, ip_hdr->src_ip, XNET_IPV4_ADDR_SIZE);
    switch(ip_hdr->protocol) {
        case XNET_PROTOCOL_UDP:
            remove_header(packet, ip_hdr_len);
            if (packet->len >= sizeof(xudp_hdr_t)) {
                xudp_hdr_t *udp_hdr = (xudp_hdr_t*)packet->data; // 不需要手动偏移了，直接转
                xudp_pcb_t *udp_socket = xudp_find_socket(swap_order16(udp_hdr->dest_port));
                if (udp_socket) {
                    // 不需要再 remove_header 了，上面已经移除了
                    xudp_in(udp_socket, &src_ip, packet);
                } else {
                    xicmp_dest_unreach(XICMP_CODE_PORT_UNREACH, ip_hdr);
                }
            }// else { 包太短连 UDP 头都不全，直接丢弃，不回 ICMP }
            break;
        case XNET_PROTOCOL_TCP:
            remove_header(packet, ip_hdr_len);
            xtcp_in(&src_ip, packet);
            break;
        case XNET_PROTOCOL_ICMP:
            printf(">> [ICMP] Recv packet from: %d.%d.%d.%d\n",
                   src_ip.addr[0], src_ip.addr[1], src_ip.addr[2], src_ip.addr[3]);
            remove_header(packet, ip_hdr_len);
            xicmp_in(&src_ip, packet);
            break;
        default:
            xicmp_dest_unreach(XICMP_CODE_PRO_UNREACH, ip_hdr);
            break;
    }
}

/**
 * 将IP数据包通过以太网发送出去
 * @param dest_ip 目标IP地址
 * @param packet 待发送IP数据包
 * @return 发送结果
 */
static xnet_status_t resolve_and_send(xip_addr_t *dest_ip, xnet_packet_t *packet) {
    xnet_status_t status;
    uint8_t *mac_addr;

    if ((status = xarp_resolve(dest_ip, &mac_addr)) == XNET_OK) {
        return ethernet_out_to(XNET_PROTOCOL_IP, mac_addr, packet);
    }
    return status;
}

xnet_status_t xip_out(xnet_protocol_t protocol, xip_addr_t *dest_ip, xnet_packet_t *packet) {
    static uint32_t ip_packet_id = 0;
    xip_hdr_t *ip_hdr;
    // 添加ip头部
    add_header(packet, sizeof(xip_hdr_t));
    ip_hdr = (xip_hdr_t*)packet->data;
    XIP_SET_VERSION_IHL(ip_hdr, XNET_VERSION_IPV4, sizeof(xip_hdr_t) / 4);
    ip_hdr->tos = 0; //不支持，填0
    ip_hdr->total_len = swap_order16(packet->len);
    ip_hdr->id = swap_order16(ip_packet_id);
    ip_hdr->flags_fragment = 0; //不支持，填0
    ip_hdr->ttl = XNET_IP_DEFAULT_TTL;
    ip_hdr->protocol = protocol;
    memcpy(ip_hdr->dest_ip, dest_ip->addr, XNET_IPV4_ADDR_SIZE);
    memcpy(ip_hdr->src_ip, xnet_local_ip.addr, XNET_IPV4_ADDR_SIZE);
    ip_hdr->hdr_checksum = 0;
    ip_hdr->hdr_checksum = checksum16((uint16_t*)ip_hdr, sizeof(xip_hdr_t), 0, 1);;

    ip_packet_id++; // packet id 每次自增
    return resolve_and_send(dest_ip, packet);
}
