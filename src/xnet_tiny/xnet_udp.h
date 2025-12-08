//
// Created by efairy520 on 2025/12/7.
//

#ifndef XNET_UDP_H
#define XNET_UDP_H

#define XUDP_CFG_MAX_UDP 10

#include "xnet_tiny.h"

typedef struct _xudp_socket_t xudp_socket_t;

// UDP 处理回调函数定义，类似于java中的接口，由业务类提供实现
typedef xnet_status_t (*xudp_handler_t) (xudp_socket_t* udp_socket, xip_addr_t* src_ip, uint16_t src_port, xnet_packet_t* packet);

struct _xudp_socket_t {
    enum {
        XUDP_STATE_FREE,
        XUDP_STATE_USED,
    } state;

    uint16_t local_port;
    xudp_handler_t handler;
};

void xudp_init(void);
xudp_socket_t* xudp_open(xudp_handler_t handler);
void xudp_close(xudp_socket_t* udp_socket);
xudp_socket_t* xudp_find(uint16_t port);
xnet_status_t xudp_bind(xudp_socket_t* udp_socket, uint16_t port);

#endif //XNET_UDP_H