/**
 * 手写 TCP/IP 协议栈
 */
#include <string.h>
#include "xnet_tiny.h"

#define min(a, b)               ((a) > (b) ? (b) : (a))

/*
* 2个字节，16位
* 翻转高8位和低8位
*/
#define swap_order16(v)   ((((v) & 0xFF) << 8) | (((v) >> 8) & 0xFF))

/**
 * mac地址
 */
static uint8_t netif_mac[XNET_MAC_ADDR_SIZE];

/**
 * 发送缓冲区
 * transmit packet
 */
static xnet_packet_t tx_packet;

/**
 * 接收缓冲区
 * receive packet
 */
static xnet_packet_t rx_packet;

/**
 * 为发包添加一个头部
 * @param packet 待处理的数据包
 * @param header_size 增加的头部大小
 */
static void add_header(xnet_packet_t *packet, uint16_t header_size) {
    // 指针前移
    packet->data -= header_size;
    packet->size += header_size;
}

/**
 * 为接收向上处理移去头部
 * @param packet 待处理的数据包
 * @param header_size 移去的头部大小
 */
static void remove_header(xnet_packet_t *packet, uint16_t header_size) {
    // 指针后移
    packet->data += header_size;
    packet->size -= header_size;
}

/**
 * 将包的长度截断为size大小
 * @param packet 待处理的数据包
 * @param size 最终大小
 */
static void truncate_packet(xnet_packet_t *packet, uint16_t size) {
    packet->size = min(packet->size, size);
}

/**
 * 分配一个网络数据包用于发送数据
 * @param size 数据空间大小
 * @return 分配得到的包结构
 */
xnet_packet_t *xnet_alloc_for_send(uint16_t size) {
    // 从tx_packet的后端往前分配，因为前边要预留作为各种协议的头部数据存储空间
    tx_packet.data = tx_packet.payload + XNET_CFG_PACKET_MAX_SIZE - size;
    tx_packet.size = size;
    return &tx_packet;
}

/**
 * 分配一个网络数据包用于读取
 * @param size 数据空间大小
 * @return 分配得到的数据包
 */
xnet_packet_t *xnet_alloc_for_read(uint16_t size) {
    // 从最开始进行分配，用于最底层的网络数据帧读取
    rx_packet.data = rx_packet.payload;
    rx_packet.size = size;
    return &rx_packet;
}

/**
 * 以太网初始化
 * @return 初始化结果
 */
static xnet_err_t ethernet_init(void) {
    xnet_err_t err = xnet_driver_open(netif_mac);
    if (err < 0) return err;

    return XNET_ERR_OK;
}

/**
 * 发送一个以太网数据帧
 * @param protocol 上层数据协议，IP或ARP
 * @param mac_addr 目标网卡的mac地址
 * @param packet 待发送的数据包
 * @return 发送结果
 */
static xnet_err_t ethernet_out_to(xnet_protocol_t protocol, const uint8_t *mac_addr, xnet_packet_t *packet) {
    xether_hdr_t *ether_hdr;

    // 添加以太网头部，指针前移
    // 传入的ether_hdr并未携带任何数据
    add_header(packet, sizeof(xether_hdr_t));
    ether_hdr = (xether_hdr_t *) packet->data;
    memcpy(ether_hdr->dest, mac_addr, XNET_MAC_ADDR_SIZE);
    memcpy(ether_hdr->src, netif_mac, XNET_MAC_ADDR_SIZE);
    ether_hdr->protocol = swap_order16(protocol);

    // 数据发送
    return xnet_driver_send(packet);
}

/**
 * 以太网数据帧输入输出
 * @param packet 待处理的包
 */
static void ethernet_in(xnet_packet_t *packet) {
    // 至少要比头部数据大
    if (packet->size <= sizeof(xether_hdr_t)) {
        return;
    }

    // 往上分解到各个协议处理
    xether_hdr_t *hdr = (xether_hdr_t *) packet->data;
    // 协议类型占用两个字节，需要大小端转换
    switch (swap_order16(hdr->protocol)) {
        case XNET_PROTOCOL_ARP:
            break;
        case XNET_PROTOCOL_IP: {
            break;
        }
    }
}

/**
 * 查询网络接口，看看是否有数据包，有则进行处理
 */
static void ethernet_poll(void) {
    xnet_packet_t *packet;
    // 此处使用二级指针，给packet赋值
    if (xnet_driver_read(&packet) == XNET_ERR_OK) {
        // 只要轮询到了数据，就会进入这里
        // 正常情况下，在此打个断点，全速运行
        // 然后在对方端ping 192.168.254.2，会停在这里
        ethernet_in(packet);
    }
}

/**
 * 协议栈的初始化
 */
void xnet_init(void) {
    ethernet_init();
}

/**
 * 轮询数据包
 */
void xnet_poll(void) {
    ethernet_poll();
}
