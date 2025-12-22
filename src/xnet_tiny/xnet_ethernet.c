//
// Created by efairy520 on 2025/10/21.
//
#include "xnet_ethernet.h"

#include "xnet_arp.h"
#include "xnet_ip.h"
#include <string.h>
#include "xnet_driver.h"


#define XARP_HW_ETHER               0x1         // 以太网
#define XARP_REQUEST                0x1         // ARP请求包
#define XARP_REPLY                  0x2         // ARP响应包

static uint8_t xnet_local_mac[XNET_MAC_ADDR_SIZE]; // 协议栈mac地址,由驱动回写
static const uint8_t ether_broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; // 以太网广播mac地址

/**
 * 发送一个以太网数据帧
 * @param protocol 上层数据协议，IP或ARP
 * @param target_mac_addr 目标网卡的mac地址
 * @param packet 待发送的数据包
 * @return 发送结果
 */
xnet_status_t ethernet_out_to(xnet_protocol_t protocol, const uint8_t* target_mac_addr, xnet_packet_t* packet) {
    // 添加以太网头部
    xether_hdr_t* ether_hdr;
    add_header(packet, sizeof(xether_hdr_t));

    // 填充头部数据
    ether_hdr = (xether_hdr_t*) packet->data;
    memcpy(ether_hdr->dest, target_mac_addr, XNET_MAC_ADDR_SIZE);
    memcpy(ether_hdr->src, xnet_local_mac, XNET_MAC_ADDR_SIZE);
    ether_hdr->protocol = swap_order16(protocol);

    // 数据发送
    return xnet_driver_send(packet);
}

/**
 * 构造一个ARP数据包，并通过以太网广播
 * @param target_ipaddr 传入目标IP，或者传自己的IP
 * @return 请求结果
 */
xnet_status_t xarp_make_request(const xip_addr_t* target_ipaddr) {
    // 准备一个发送包
    xarp_packet_t* arp_packet;
    xnet_packet_t* xnet_packet = xnet_alloc_tx_packet(sizeof(xarp_packet_t));

    // 让 arp_packet 指向 data 首地址，配置载荷
    arp_packet = (xarp_packet_t*) xnet_packet->data;
    arp_packet->hardware_type = swap_order16(XARP_HW_ETHER);
    arp_packet->protocol_type = swap_order16(XNET_PROTOCOL_IP);
    arp_packet->hardware_len = XNET_MAC_ADDR_SIZE;
    arp_packet->protocol_len = XNET_IPV4_ADDR_SIZE;
    arp_packet->opcode = swap_order16(XARP_REQUEST);
    memcpy(arp_packet->sender_mac, xnet_local_mac, XNET_MAC_ADDR_SIZE);
    memcpy(arp_packet->sender_ip, xnet_local_ip.addr, XNET_IPV4_ADDR_SIZE);
    memset(arp_packet->target_mac, 0, XNET_MAC_ADDR_SIZE);
    memcpy(arp_packet->target_ip, target_ipaddr->addr, XNET_IPV4_ADDR_SIZE);
    // 发送ARP请求，多播
    return ethernet_out_to(XNET_PROTOCOL_ARP, ether_broadcast_mac, xnet_packet);
}

/**
 * 以太网初始化，此时会写入协议栈 mac 地址
 * @return 初始化结果
 */
xnet_status_t ethernet_init(void) {
    xnet_status_t status = xnet_driver_open(xnet_local_mac);
    if (status < 0) return status;
    // 全网广播自己的 mac 地址，target ip设置自己
    return xarp_make_request(&xnet_local_ip);
}

/**
 * 生成一个ARP响应
 * @param target_ip
 * @param target_mac
 * @param arp_in_packet 接收到的ARP请求包
 * @return 生成结果
 */
xnet_status_t xarp_make_response(uint8_t* target_ip, uint8_t* target_mac) {
    xarp_packet_t* arp_packet;
    xnet_packet_t* packet = xnet_alloc_tx_packet(sizeof(xarp_packet_t));

    arp_packet = (xarp_packet_t*) packet->data;
    arp_packet->hardware_type = swap_order16(XARP_HW_ETHER);
    arp_packet->protocol_type = swap_order16(XNET_PROTOCOL_IP);
    arp_packet->hardware_len = XNET_MAC_ADDR_SIZE;
    arp_packet->protocol_len = XNET_IPV4_ADDR_SIZE;
    arp_packet->opcode = swap_order16(XARP_REPLY);
    memcpy(arp_packet->sender_mac, xnet_local_mac, XNET_MAC_ADDR_SIZE);
    memcpy(arp_packet->sender_ip, xnet_local_ip.addr, XNET_IPV4_ADDR_SIZE);
    memcpy(arp_packet->target_mac, target_mac, XNET_MAC_ADDR_SIZE);
    memcpy(arp_packet->target_ip, target_ip, XNET_IPV4_ADDR_SIZE);
    // 发送ARP响应，单播
    return ethernet_out_to(XNET_PROTOCOL_ARP, target_mac, packet);
}

/**
 * ARP输入处理
 * @param packet 输入的ARP包
 */
void xarp_in(xnet_packet_t* packet) {
    // 如果小于，说明数据错误，直接忽略这个arp请求
    if (packet->length < sizeof(xarp_packet_t)) return;

    // 包的合法性检查
    xarp_packet_t* arp_packet = (xarp_packet_t*) packet->data;
    uint16_t opcode = swap_order16(arp_packet->opcode);
    if ((swap_order16(arp_packet->hardware_type) != XARP_HW_ETHER) ||
        (arp_packet->hardware_len != XNET_MAC_ADDR_SIZE) ||
        (swap_order16(arp_packet->protocol_type) != XNET_PROTOCOL_IP) ||
        (arp_packet->protocol_len != XNET_IPV4_ADDR_SIZE)
        || ((opcode != XARP_REQUEST) && (opcode != XARP_REPLY))) {
        return;
    }

    // 处理无偿ARP
    if (xip_addr_eq(arp_packet->sender_ip, arp_packet->target_ip)) {
        update_arp_entry(arp_packet->sender_ip, arp_packet->sender_mac);
        return;
    }

    // 只处理发给自己的ARP
    if (!xip_addr_eq(xnet_local_ip.addr, arp_packet->target_ip)) {
        return;
    }


    // 根据操作码进行不同的处理
    switch (swap_order16(arp_packet->opcode)) {
        case XARP_REQUEST: // 收到请求，回送响应
            // 在对方机器Ping 自己，然后看wireshark，能看到ARP请求和响应
            // 接下来，很可能对方要与自己通信，所以更新一下
            update_arp_entry(arp_packet->sender_ip, arp_packet->sender_mac);
            xarp_make_response(arp_packet->sender_ip, arp_packet->sender_mac);
            break;
        case XARP_REPLY: // 收到响应，更新自己的表
            update_arp_entry(arp_packet->sender_ip, arp_packet->sender_mac);
            break;
    }

}

/**
 * 以太网数据帧输入输出
 * @param packet 待处理的包
 */
void ethernet_in(xnet_packet_t* packet) {
    // 数据至少要比以太网头部大
    if (packet->length <= sizeof(xether_hdr_t)) {
        return;
    }

    // 往上分解到各个协议处理
    xether_hdr_t* ether_hdr = (xether_hdr_t*) packet->data;
    // 协议类型占用两个字节，需要大小端转换
    switch (swap_order16(ether_hdr->protocol)) {
        case XNET_PROTOCOL_ARP:
            remove_header(packet, sizeof(xether_hdr_t));
            xarp_in(packet);
            break;
        case XNET_PROTOCOL_IP: {
            // 避免客户端第一次发送请求没有响应
            xip_hdr_t* ip_hdr = (xip_hdr_t*)(packet->data + sizeof(xether_hdr_t));
            update_arp_entry(ip_hdr->src_ip, ether_hdr->src);
            remove_header(packet, sizeof(xether_hdr_t));
            xip_in(packet);
            break;
        }
    }
}

/**
 * 查询网络接口，看看是否有数据包，有则进行处理
 */
void ethernet_poll(void) {
    xnet_packet_t* packet;
    // 此处使用二级指针，给packet赋值
    if (xnet_driver_read(&packet) == XNET_OK) {
        // 只要轮询到了数据，就会进入这里
        // 正常情况下，在此打个断点，全速运行
        // 然后在对方端ping 192.168.254.2，会停在这里
        ethernet_in(packet);
    }
}