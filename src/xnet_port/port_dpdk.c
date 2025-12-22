/**
* src/xnet_port/port_dpdk.c
 * 适配层：连接 xnet 协议栈 和 DPDK Driver
 */
#include "xnet_tiny.h"
#include "xnet_driver.h"
#include "dpdk_device.h" // 只依赖我们自己定义的头文件
#include <time.h>        // clock_gettime

xnet_status_t xnet_driver_open(uint8_t* mac_addr) {
    printf(">> [System Info] Initializing Driver: Linux DPDK\n");
    // 1. 调用底层驱动初始化
    if (dpdk_device_init() != DPDK_OK) {
        return XNET_ERR_IO;
    }

    // 2. 获取 MAC 地址
    dpdk_device_get_mac(mac_addr);
    return XNET_OK;
}

xnet_status_t xnet_driver_send(xnet_packet_t* packet) {
    // 直接把协议栈的数据透传给驱动
    int ret = dpdk_device_send(packet->data, packet->length);
    if (ret > 0) {
        return XNET_OK;
    }
    return XNET_ERR_IO;
}

xnet_status_t xnet_driver_read(xnet_packet_t** packet) {
    // 临时缓冲区，用于接收底层数据
    // 1514 是以太网最大帧长
    static uint8_t rx_buffer[1514];

    // 1. 尝试从驱动读数据
    int len = dpdk_device_read(rx_buffer, sizeof(rx_buffer));

    if (len > 0) {
        // 2. 如果读到了，向协议栈申请内存
        xnet_packet_t* r_packet = xnet_alloc_rx_packet(len);

        // 3. 填充数据
        memcpy(r_packet->data, rx_buffer, len);
        r_packet->length = len;

        *packet = r_packet;
        return XNET_OK;
    }

    return XNET_ERR_IO; // 没数据，或者出错
}