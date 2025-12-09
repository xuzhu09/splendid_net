/**
 * 手写 TCP/IP 协议栈
 */
#include "xnet_tiny.h"
#include "xnet_ethernet.h"
#include "xnet_arp.h"
#include "xnet_ip.h"
#include "xnet_icmp.h"
#include "xnet_tcp.h"
#include "xnet_udp.h"

static xnet_packet_t tx_packet, rx_packet; // 接收与发送缓冲区
const xip_addr_t xnet_local_ip = XNET_CFG_DEFAULT_IP; // 协议栈的IP地址

/**
 * 为发包添加一个头部
 * @param packet 待处理的数据包
 * @param header_size 增加的头部大小
 */
void add_header(xnet_packet_t* packet, uint16_t header_size) {
    // 指针前移
    packet->data -= header_size;
    packet->length += header_size;
}

/**
 * 为接收向上处理移去头部
 * @param packet 待处理的数据包
 * @param header_size 移去的头部大小
 */
void remove_header(xnet_packet_t* packet, uint16_t header_size) {
    // 指针后移
    packet->data += header_size;
    packet->length -= header_size;
}

/**
 * 将包的长度截断为size大小
 * @param packet 待处理的数据包
 * @param size 最终大小
 */
void truncate_packet(xnet_packet_t* packet, uint16_t size) {
    packet->length = min(packet->length, size);
}

/**
 * 准备一个网络数据包用于发送数据
 * @param size 数据空间大小
 * @return 分配得到的包结构
 */
xnet_packet_t* xnet_alloc_tx_packet(uint16_t size) {
    // 从tx_packet的后端往前分配，因为前边要预留作为各种协议的头部数据存储空间
    tx_packet.data = tx_packet.buffer + XNET_CFG_PACKET_MAX_SIZE - size;
    tx_packet.length = size;
    return &tx_packet;
}

/**
 * 准备一个网络数据包用于读取
 * @param size 数据空间大小
 * @return 分配得到的数据包
 */
xnet_packet_t* xnet_alloc_rx_packet(uint16_t size) {
    // 从最开始进行分配，用于最底层的网络数据帧读取
    rx_packet.data = rx_packet.buffer;
    rx_packet.length = size;
    return &rx_packet;
}

/**
 * 协议栈的初始化
 */
void xnet_init(void) {
    ethernet_init();
    xarp_init();
    xip_init();
    xicmp_init();
    xudp_init();
    xtcp_init();
}

/**
 * 轮询数据包
 */
void xnet_poll(void) {
    ethernet_poll();
    xarp_poll();
}
