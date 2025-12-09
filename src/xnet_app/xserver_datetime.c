#include "xserver_datetime.h"
#include <time.h>
#include <string.h>

#include "xnet_udp.h"

#define TIME_STR_SIZE         128

xnet_status_t datetime_handler(xudp_socket_t * udp_socket, xip_addr_t* src_ip, uint16_t src_port, xnet_packet_t* packet) {
    time_t rawtime;
    const struct tm * timeinfo;
    xnet_packet_t* tx_packet;
    size_t str_size;

    tx_packet = xnet_alloc_tx_packet(TIME_STR_SIZE);

    time(&rawtime);
    timeinfo = localtime(&rawtime);
    // 先改成最简单的格式测试
    str_size = strftime((char*)tx_packet->data, TIME_STR_SIZE, "%Y-%m-%d %H:%M:%S", timeinfo);
    truncate_packet(tx_packet, str_size);

    // 发送
    return xudp_send_to(udp_socket, src_ip, src_port, tx_packet);
}

xnet_status_t xserver_datetime_create(uint16_t port) {
    xudp_socket_t * udp = xudp_alloc_socket(datetime_handler);
    xudp_bind_socket(udp, port);
    return XNET_OK;
}