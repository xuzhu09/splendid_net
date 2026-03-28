//
// Created by efairy520 on 2025/10/21.
//
#include "xnet_ethernet.h"

#include "xnet_arp.h"
#include "xnet_ip.h"
#include <string.h>
#include "xnet_netif.h"

const uint8_t ether_broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; // 以太网广播mac地址

// 关闭填充字节
#pragma pack(1)
// 以太网头部 14个字节
typedef struct _xether_hdr_t {
    uint8_t dest[XNET_MAC_ADDR_SIZE];                   // 目标mac地址，6字节
    uint8_t src[XNET_MAC_ADDR_SIZE];                    // 源mac地址，6字节
    uint16_t protocol;                                  // 上层协议，2字节
} xether_hdr_t;
#pragma pack()

/**
 * 以太网数据帧输入输出
 * @param packet 待处理的包
 */
static void ethernet_in(xnet_packet_t *packet) {
    // 数据至少要比以太网头部大
    if (packet->len <= sizeof(xether_hdr_t)) {
        return;
    }

    // 往上分解到各个协议处理
    xether_hdr_t *ether_hdr = (xether_hdr_t*) packet->data;
    // 协议类型占用两个字节，需要大小端转换
    switch (swap_order16(ether_hdr->protocol)) {
        case XNET_PROTOCOL_ARP:
            remove_header(packet, sizeof(xether_hdr_t));
            xarp_in(packet);
            break;
        case XNET_PROTOCOL_IP: {
            remove_header(packet, sizeof(xether_hdr_t));
            xip_in(packet);
            break;
        }
    }
}

/**
 * 发送一个以太网数据帧
 * @param protocol 上层数据协议，IP或ARP
 * @param target_mac_addr 目标网卡的mac地址
 * @param packet 待发送的数据包
 * @return 发送结果
 */
xnet_status_t ethernet_out_to(xnet_protocol_t protocol, const uint8_t *target_mac_addr, xnet_packet_t *packet) {
    // 添加以太网头部
    xether_hdr_t *ether_hdr;
    add_header(packet, sizeof(xether_hdr_t));

    // 填充头部数据
    ether_hdr = (xether_hdr_t*) packet->data;
    memcpy(ether_hdr->dest, target_mac_addr, XNET_MAC_ADDR_SIZE);
    memcpy(ether_hdr->src, xnet_local_mac, XNET_MAC_ADDR_SIZE);
    ether_hdr->protocol = swap_order16(protocol);

    // 数据发送
    return xnet_netif_send(packet);
}

/**
 * 以太网初始化，此时会写入协议栈 mac 地址
 * @return 初始化结果
 */
xnet_status_t ethernet_init(void) {
    xnet_status_t status = xnet_netif_open(xnet_local_mac);
    if (status < 0) return status;
    // 全网广播自己的 mac 地址，target ip设置自己
    return xarp_make_request(&xnet_local_ip);
}

/**
 * 查询网络接口，看看是否有数据包，有则进行处理
 */
void ethernet_poll(void) {
    xnet_packet_t *packet;
    // 此处使用二级指针，给packet赋值
    if (xnet_netif_read(&packet) == XNET_OK) {
        // 只要轮询到了数据，就会进入这里
        // 正常情况下，在此打个断点，全速运行
        // 然后在对方端ping 192.168.254.2，会停在这里
        ethernet_in(packet);
    }
}