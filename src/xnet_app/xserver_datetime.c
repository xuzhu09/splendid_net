#include "xserver_datetime.h"

#include <stdio.h>

#include "xsocket.h"

#include <time.h>
#include <string.h>

#define TIME_STR_SIZE 128

static xsocket_t *udp_socket;
static char time_buffer[TIME_STR_SIZE];
static char rx_buf[64];

xnet_status_t xserver_datetime_create(uint16_t port) {
    udp_socket = xsocket_open_ex(XSOCKET_TYPE_UDP);
    if (!udp_socket) return XNET_ERR_MEM;

    xnet_status_t r = xsocket_bind(udp_socket, port);
    if (r != XNET_OK) {
        xsocket_close(udp_socket);
        udp_socket = NULL;
        return r;
    }
    return XNET_OK;
}

void xserver_datetime_poll(void) {
    if (!udp_socket) return;

    xip_addr_t src_ip;
    uint16_t src_port;

    // 收到任意 UDP 包就回时间；max_polls 给小一点避免卡主循环
    int n = xsocket_recvfrom(udp_socket, rx_buf, sizeof(rx_buf), &src_ip, &src_port, 1);
    if (n <= 0) return;

    printf("[DateTime Server] Recv UDP Request from IP: %d.%d.%d.%d, Port: %d\n",
           src_ip.addr[0], src_ip.addr[1], src_ip.addr[2], src_ip.addr[3],
           src_port);

    time_t rawtime;
    struct tm *timeinfo;
    time(&rawtime);
    timeinfo = localtime(&rawtime);

    int len = 0;
    if (timeinfo) {
        len = (int)strftime(time_buffer, TIME_STR_SIZE, "%Y-%m-%d %H:%M:%S\r\n", timeinfo);
    }

    if (len <= 0) {
        const char *fallback = "1970-01-01 00:00:00\r\n";
        strncpy(time_buffer, fallback, TIME_STR_SIZE - 1);
        time_buffer[TIME_STR_SIZE - 1] = '\0';
        len = (int)strlen(time_buffer);
    }

    xsocket_sendto(udp_socket, time_buffer, len, &src_ip, src_port);
}
