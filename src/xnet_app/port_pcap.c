/**
 * 协议栈 ↔ 网卡 适配层
 */
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include "pcap_device.h"
#include "xnet_tiny.h"

static pcap_t* pcap;

/**
 * pcap 所用的真实网卡，物理网卡
 * 一般为主机 ip 地址
 */
const char *ip_str = "192.168.254.1";

/**
 * 协议栈虚拟 mac
 *
 */
const char default_mac_addr[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};

/**
 * 初始化网络驱动
 * @return 0成功，其它失败
 */
xnet_status_t xnet_driver_open(uint8_t* mac_addr) {
    memcpy(mac_addr, default_mac_addr, sizeof(default_mac_addr));
    pcap = pcap_device_open(ip_str, mac_addr, 1);
    if (pcap == (pcap_t*) 0) {
        exit(-1);
    }
    return XNET_OK;
}

/**
 * 发送数据
 * @param frame 数据起始地址
 * @param size 数据长度
 * @return 0 - 成功，其它失败
 */
xnet_status_t xnet_driver_send(xnet_packet_t* packet) {
    return pcap_device_send(pcap, packet->data, packet->length);
}

/**
 * 读取数据
 * @param frame 数据存储位置
 * @param size 数据长度
 * @return 0 - 成功，其它失败
 */
xnet_status_t xnet_driver_read(xnet_packet_t** packet) {
    uint16_t size;
    xnet_packet_t* r_packet = xnet_alloc_rx_packet(XNET_CFG_PACKET_MAX_SIZE);

    size = pcap_device_read(pcap, r_packet->data, XNET_CFG_PACKET_MAX_SIZE);
    if (size) {
        r_packet->length = size;
        *packet = r_packet;
        return XNET_OK;
    }

    return XNET_ERR_IO;
}

/**
 * 获取自程序启动以来，过去了多长时间
 * @return 程序的系统时间
 */
const xnet_time_t xsys_get_time(void) {
    static uint32_t pre = 0;

    // 以下部分仅供调试时使用
#if 0
    uint32_t c = clock() / CLOCKS_PER_SEC;
    if (c != pre) {
        printf("sec: %d, 100ms: %d\n", c, (xnet_time_t)(clock()  * 10 / CLOCKS_PER_SEC));
        pre = c;
    }
#endif
    return (xnet_time_t)(clock() / CLOCKS_PER_SEC);
}
