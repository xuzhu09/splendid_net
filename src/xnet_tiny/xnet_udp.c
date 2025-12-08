//
// Created by efairy520 on 2025/12/7.
//

#include "xnet_udp.h"

#include <string.h>

static xudp_socket_t udp_socket_pool[XUDP_CFG_MAX_UDP];

void xudp_init(void) {
    memset(udp_socket_pool, 0, sizeof(udp_socket_pool));
}

xudp_socket_t* xudp_open(xudp_handler_t handler) {
    // 1. 遍历资源池
    for (xudp_socket_t* cur = udp_socket_pool; cur < &udp_socket_pool[XUDP_CFG_MAX_UDP]; cur++) {

        // 2. 检查是否占用
        if (cur->state == XUDP_STATE_FREE) {
            cur->state = XUDP_STATE_USED;
            cur->local_port = 0; // 端口先置 0，表示还没绑定具体端口（稍后调用 bind）
            cur->handler = handler; // 挂载回调函数
            return cur;
        }
    }
    return NULL;
}

void xudp_close(xudp_socket_t* udp_socket) {
    udp_socket->state = XUDP_STATE_FREE;
}

xudp_socket_t* xudp_find(uint16_t port) {
    for (xudp_socket_t* curr = udp_socket_pool; curr < &udp_socket_pool[XUDP_CFG_MAX_UDP]; curr++) {
        if (curr->state == XUDP_STATE_USED && curr->local_port == port) {
            return curr;
        }
    }
    return NULL;
}

xnet_status_t xudp_bind(xudp_socket_t* udp_socket, uint16_t port) {
    // 1. 是否已占用
    for (xudp_socket_t* curr = udp_socket_pool; curr < &udp_socket_pool[XUDP_CFG_MAX_UDP]; curr++) {
        if (curr->state == XUDP_STATE_USED && curr->local_port == port) {
            return XNET_ERR_BINDED;
        }
    }
    // 2. 绑定
    udp_socket->local_port = port;
    return XNET_OK;
}