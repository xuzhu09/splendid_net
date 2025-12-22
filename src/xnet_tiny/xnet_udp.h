//
// Created by efairy520 on 2025/12/7.
//

#ifndef XNET_UDP_H
#define XNET_UDP_H

#include "xnet_def.h"

#define XUDP_MAX_SOCKET_COUNT 10

typedef struct _xudp_socket_t xudp_socket_t;

// UDP 处理回调函数定义，类似于java中的接口，由业务类提供实现
typedef xnet_status_t (*xudp_handler_t) (xudp_socket_t* udp_socket, xip_addr_t* src_ip, uint16_t src_port, xnet_packet_t* packet);

// 控制块，不需要发送到网络，可以padding
struct _xudp_socket_t {
    enum {
        XUDP_STATE_FREE,
        XUDP_STATE_USED,
    } state;

    uint16_t local_port;
    xudp_handler_t handler;
};

#pragma pack(1)
// UDP头部，需发送到网络，禁用padding
typedef struct _xudp_hdr_t {
    uint16_t src_port;
    uint16_t dest_port;
    uint16_t total_len;
    uint16_t checksum;
} xudp_hdr_t;
#pragma pack()

void xudp_init(void);

xudp_socket_t* xudp_alloc_socket(xudp_handler_t handler);
xnet_status_t xudp_bind_socket(xudp_socket_t* socket, uint16_t port);
xudp_socket_t* xudp_find_socket(uint16_t port);
void xudp_free_socket(xudp_socket_t* socket);

void xudp_in(xudp_socket_t* socket, xip_addr_t* src_ip, xnet_packet_t* packet);
xnet_status_t xudp_send_to(xudp_socket_t* socket, xip_addr_t* dest_ip, uint16_t dest_port, xnet_packet_t* packet);

#endif //XNET_UDP_H