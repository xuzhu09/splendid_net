#include "xnet_tiny.h"
#include "tap_device.h"
#include <string.h>
#include <stdio.h>

xnet_status_t xnet_netif_open(uint8_t* mac_addr) {
    printf(">> [System Info] Initializing Driver: Linux TAP\n");

    // 初始化名为 tap0 的虚拟网卡
    if (tap_device_init("tap0") != TAP_OK) {
        return XNET_ERR_IO;
    }

    tap_device_get_mac(mac_addr);
    return XNET_OK;
}

xnet_status_t xnet_netif_send(xnet_packet_t* packet) {
    int ret = tap_device_send(packet->data, packet->len);
    if (ret > 0) {
        return XNET_OK;
    }
    return XNET_ERR_IO;
}

xnet_status_t xnet_netif_read(xnet_packet_t** packet) {
    static uint8_t rx_buffer[1514];

    int len = tap_device_read(rx_buffer, sizeof(rx_buffer));

    if (len > 0) {
        xnet_packet_t* r_packet = xnet_alloc_rx_packet(len);
        memcpy(r_packet->data, rx_buffer, len);
        r_packet->len = len;

        *packet = r_packet;
        return XNET_OK;
    }

    return XNET_ERR_IO;
}