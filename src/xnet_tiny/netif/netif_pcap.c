/**
 * 协议栈 ↔ 网卡 适配层
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include "pcap_device.h"
#include "xnet_tiny.h"
#include "xnet_netif.h"
#include "xnet_config.h"

static pcap_t* pcap;

/**
 * 协议栈虚拟 mac
 * 第一个字节0结尾单播，1结尾多播，所以不要用0x11(00010001)
 */
const char default_mac_addr[] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};

/**
 * 初始化网络驱动
 * @return 0成功，其它失败
 */
xnet_status_t xnet_netif_open(uint8_t* mac_addr) {
    printf(">> [System Info] Initializing Driver: Windows Pcap\n");
    memcpy(mac_addr, default_mac_addr, sizeof(default_mac_addr));

    // 使用网关ip寻找网卡
    uint8_t target_ip[] = CFG_IP_GW;

    char target_ip_str[16];
    sprintf(target_ip_str, "%d.%d.%d.%d",
            target_ip[0], target_ip[1],
            target_ip[2], target_ip[3]);

    // 打印出来确认一下，你会发现不管是不是 DHCP，这里永远打印静态锚点 IP
    printf(">> [Driver] Passing IP to PCAP locator: %s\n", target_ip_str);

    pcap = pcap_device_open(target_ip_str, mac_addr, 1);
    if (pcap == (pcap_t*) 0) {
        printf(">> [Driver Error] Failed to open network device.\n");
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
xnet_status_t xnet_netif_send(xnet_packet_t* packet) {
    return pcap_device_send(pcap, packet->data, packet->len);
}

/**
 * 读取数据
 * @param frame 数据存储位置
 * @param size 数据长度
 * @return 0 - 成功，其它失败
 */
xnet_status_t xnet_netif_read(xnet_packet_t** packet) {
    uint16_t size;
    xnet_packet_t* r_packet = xnet_alloc_rx_packet(XNET_CFG_PACKET_MAX_SIZE);

    size = pcap_device_read(pcap, r_packet->data, XNET_CFG_PACKET_MAX_SIZE);
    if (size) {
        r_packet->len = size;
        *packet = r_packet;
        return XNET_OK;
    }

    return XNET_ERR_IO;
}

