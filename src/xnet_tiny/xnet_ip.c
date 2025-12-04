//
// Created by efairy520 on 2025/10/26.
//
#include "xnet_ip.h"

#include <string.h>
#include "xnet_arp.h"
#include "xnet_ethernet.h"

/**
 * 校验和计算
 * @param buf 校验数据区的起始地址
 * @param len 数据区的长度，以字节为单位
 * @param pre_sum 累加的之前的值，用于多次调用checksum对不同的的数据区计算出一个校验和
 * @param complement 是否对累加和的结果进行取反
 * @return 校验和结果
 */
static uint16_t checksum16(uint16_t * buf, uint16_t len, uint16_t pre_sum, int complement) {
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

void xip_init(void) {

}

void xip_in(xnet_packet_t* packet) {
    xip_hdr_t* iphdr = (xip_hdr_t*) packet->data_start;
    uint32_t total_size, header_size;
    uint16_t pre_checksum;
    xip_addr_u src_ip;

    // 进行一些必要性的检查：版本号要求
    if (iphdr->version != XNET_VERSION_IPV4) {
        return;
    }

    // 长度要求检查
    header_size = iphdr->hdr_len * 4;
    total_size = swap_order16(iphdr->total_len);
    if ((header_size < sizeof(xip_hdr_t)) || ((total_size < header_size) || (packet->data_length < total_size))) {
        return;
    }

    // 校验和要求检查
    pre_checksum = iphdr->hdr_checksum; //取出原校验和
    iphdr->hdr_checksum = 0; //校验和本身也会参与运算，先归零
    if (pre_checksum != checksum16((uint16_t*)iphdr, header_size, 0, 1)) {
        return;
    }

    // 只处理目标IP为自己的数据包，其它广播之类的IP全部丢掉
    if (!xipaddr_is_equal_buf(netif_ipaddr.array, iphdr->dest_ip)) {
        return;
    }

    // xip4_addr_from_buf(&src_ip, iphdr->src_ip);
    // switch(iphdr->protocol) {
    //     case XNET_PROTOCOL_ICMP:
    //         remove_header(packet, header_size);
    //         xicmp_in(&src_ip, packet);
    //         break;
    //     default:
    //         break;
    // }
}

/**
 * 将IP数据包通过以太网发送出去
 * @param dest_ip 目标IP地址
 * @param packet 待发送IP数据包
 * @return 发送结果
 */
static xnet_status_t resolve_and_send(xip_addr_u* dest_ip, xnet_packet_t* packet) {
    xnet_status_t err;
    uint8_t* mac_addr;

    if ((err = xarp_resolve(dest_ip, &mac_addr) == XNET_OK)) {
        return ethernet_out_to(XNET_PROTOCOL_IP, mac_addr, packet);
    }
    return err;
}

xnet_status_t xip_out(xnet_protocol_t protocol, xip_addr_u* dest_ip, xnet_packet_t * packet) {
    static uint32_t ip_packet_id = 0;
    xip_hdr_t * iphdr;

    add_header(packet, sizeof(xip_hdr_t));
    iphdr = (xip_hdr_t*)packet->data_start;
    iphdr->version = XNET_VERSION_IPV4;
    iphdr->hdr_len = sizeof(xip_hdr_t) / 4;
    iphdr->tos = 0; //不支持，填0
    iphdr->total_len = swap_order16(packet->data_length);
    iphdr->id = swap_order16(ip_packet_id);
    iphdr->flags_fragment = 0; //不支持，填0
    iphdr->ttl = XNET_IP_DEFAULT_TTL;
    iphdr->protocol = protocol;
    memcpy(iphdr->dest_ip, dest_ip->array, XNET_IPV4_ADDR_SIZE);
    memcpy(iphdr->src_ip, netif_ipaddr.array, XNET_IPV4_ADDR_SIZE);
    iphdr->hdr_checksum = 0;
    iphdr->hdr_checksum = checksum16((uint16_t *)iphdr, sizeof(xip_hdr_t), 0, 1);;

    ip_packet_id++;
    return resolve_and_send(dest_ip, packet);
}
