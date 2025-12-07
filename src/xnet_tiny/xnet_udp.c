//
// Created by efairy520 on 2025/12/7.
//

#include "xnet_udp.h"

#include <string.h>

static xudp_t udp_socket_pool[XUDP_CFG_MAX_UDP];

void xudp_init(void) {
    memset(udp_socket_pool, 0, sizeof(udp_socket_pool));
}