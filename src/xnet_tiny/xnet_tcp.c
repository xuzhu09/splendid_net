//
// Created by efairy520 on 2025/12/9.
//

#include "xnet_tcp.h"

#include <string.h>

static xtcp_socket_t tcp_socket_pool[XTCP_MAX_SOCKET_COUNT];

void xtcp_init(void) {
    memset(tcp_socket_pool, 0, sizeof(tcp_socket_pool));
}

xtcp_socket_t* xtcp_alloc_socket(void) {
    for (xtcp_socket_t* curr = tcp_socket_pool; curr < &tcp_socket_pool[XTCP_MAX_SOCKET_COUNT]; curr++) {
        if (curr->state == XTCP_STATE_FREE) {
            return curr;
        }
    }
    return NULL;
}

void xtcp_free_socket(xtcp_socket_t* socket) {
    socket->state = XTCP_STATE_FREE;
}
