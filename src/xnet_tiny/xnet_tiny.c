/**
* src/xnet_tiny/xnet_tiny.c
 * 手写 TCP/IP 协议栈核心实现
 */
#include "xnet_tiny.h"

#include <stdlib.h>
#include <time.h>
#include "xnet_ethernet.h"
#include "xnet_arp.h"
#include "xnet_ip.h"
#include "xnet_icmp.h"
#include "xnet_tcp.h"
#include "xnet_udp.h"

// 接收与发送缓冲区 (整个协议栈的核心静态资源)
static xnet_packet_t tx_packet, rx_packet;

// 协议栈的IP地址
const xip_addr_t xnet_local_ip = XNET_CFG_DEFAULT_IP;

/**
 * 为发包添加一个头部
 */
void add_header(xnet_packet_t* packet, uint16_t header_size) {
    packet->data -= header_size;
    packet->length += header_size;
}

/**
 * 为接收向上处理移去头部
 */
void remove_header(xnet_packet_t* packet, uint16_t header_size) {
    packet->data += header_size;
    packet->length -= header_size;
}

/**
 * 将包的长度截断为size大小
 */
void truncate_packet(xnet_packet_t* packet, uint16_t size) {
    packet->length = min(packet->length, size);
}

/**
 * 准备一个网络数据包用于发送数据
 * 从tx_packet的后端往前分配，预留头部空间
 */
xnet_packet_t* xnet_alloc_tx_packet(uint16_t size) {
    tx_packet.data = tx_packet.buffer + XNET_CFG_PACKET_MAX_SIZE - size;
    tx_packet.length = size;
    return &tx_packet;
}

/**
 * 准备一个网络数据包用于读取
 * 从最开始进行分配
 */
xnet_packet_t* xnet_alloc_rx_packet(uint16_t size) {
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
    srand(xsys_get_time());
}

/**
 * 轮询数据包
 */
void xnet_poll(void) {
    ethernet_poll();
    xarp_poll();
}

/**
 * 获取自程序启动以来，过去了多长时间
 * 使用墙钟时间，而不是CPU时间
 * 原代码使用 clock()。在 Windows 上它近似于“墙钟时间”，但在 Linux 上它严格遵循 POSIX 标准返回 CPU 时间
 * @return 程序的系统时间
 */
const xnet_time_t xsys_get_time(void) {
    struct timespec ts;
    // CLOCK_MONOTONIC 保证时间单调递增，不受修改系统时间影响
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec;
}