/**
* src/xnet_tiny/xnet_tiny.c
 * 手写 TCP/IP 协议栈核心实现
 */
#include "xnet_tiny.h"

#include <stdlib.h>
#include "xnet_ethernet.h"
#include "xnet_arp.h"
#include "xnet_ip.h"
#include "xnet_icmp.h"
#include "xnet_tcp.h"
#include "xnet_udp.h"

// 接收与发送缓冲区 (整个协议栈的核心静态资源)
static xnet_packet_t tx_packet, rx_packet;

// 提供一个默认兜底方案，防止宏没传进来导致编译报错
#ifndef XNET_CFG_LOCAL_IP
    #define XNET_CFG_LOCAL_IP 127, 0, 0, 1
#endif

// 协议栈的IP地址
xip_addr_t xnet_local_ip = {{XNET_CFG_LOCAL_IP}};
// 新增掩码和网关 (暂时给个默认值，后续由 DHCP 覆盖)
xip_addr_t xnet_netmask  = {{255, 255, 255, 0}};
xip_addr_t xnet_gateway  = {{0, 0, 0, 0}};

/**
 * 为发包添加一个头部
 */
void add_header(xnet_packet_t *packet, uint16_t header_size) {
    packet->data -= header_size;
    packet->len += header_size;
}

/**
 * 为接收向上处理移去头部
 */
void remove_header(xnet_packet_t *packet, uint16_t header_size) {
    packet->data += header_size;
    packet->len -= header_size;
}

/**
 * 将包的长度截断为size大小
 */
void truncate_packet(xnet_packet_t *packet, uint16_t new_len) {
    packet->len = min(packet->len, new_len);
}

/**
 * 准备一个网络数据包用于发送数据
 * 从tx_packet的后端往前分配，预留头部空间
 */
xnet_packet_t *xnet_alloc_tx_packet(uint16_t size) {
    tx_packet.data = tx_packet.buffer + XNET_CFG_PACKET_MAX_SIZE - size;
    tx_packet.len = size;
    return &tx_packet;
}

/**
 * 准备一个网络数据包用于读取
 * 从最开始进行分配
 */
xnet_packet_t *xnet_alloc_rx_packet(uint16_t size) {
    rx_packet.data = rx_packet.buffer;
    rx_packet.len = size;
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
    srand(xsys_get_time());
}

/**
 * 轮询数据包
 */
void xnet_poll(void) {
    ethernet_poll();
    xarp_poll();
}

// 全局通用的超时检测工具
int xnet_check_tmo(xnet_time_t *last_time, uint32_t gap_time) {
    xnet_time_t curr_time = xsys_get_time(); // 调用底层提供的契约函数

    if (gap_time == 0) {
        *last_time = curr_time;
        return 0;
    }

    if (curr_time - *last_time >= gap_time) {
        *last_time = curr_time;
        return 1;
    }
    return 0;
}