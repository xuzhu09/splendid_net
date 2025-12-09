//
// Created by efairy520 on 2025/12/9.
//

#include "xnet_tcp.h"
#include <string.h>

// 静态资源池
static xtcp_socket_t tcp_socket_pool[XTCP_MAX_SOCKET_COUNT];

void xtcp_init(void) {
    // 整体清零，确保所有状态为 XTCP_STATE_FREE (0)
    memset(tcp_socket_pool, 0, sizeof(tcp_socket_pool));
}

xtcp_socket_t* xtcp_alloc_socket(xtcp_event_handler_t handler) {
    for (xtcp_socket_t* curr = tcp_socket_pool; curr < &tcp_socket_pool[XTCP_MAX_SOCKET_COUNT]; curr++) {
        // 找到一个空闲的 Socket
        if (curr->state == XTCP_STATE_FREE) {
            // 【优化点1】：必须清空旧数据！
            // Socket被回收后，里面可能残留着上一次连接的 IP、端口等脏数据。
            //如果不清零，下次 bind 可能出错，或者调试时看到奇怪的数值。
            memset(curr, 0, sizeof(xtcp_socket_t));

            // 初始化状态和回调
            curr->state = XTCP_STATE_CLOSED;
            curr->handler = handler;
            return curr;
        }
    }
    return NULL;
}

xnet_status_t xtcp_bind_socket(xtcp_socket_t* socket, uint16_t local_port) {
    if (socket == NULL || local_port == 0) {
        return XNET_ERR_PARAM;
    }

    // 1. 检查端口冲突
    for (xtcp_socket_t* curr = tcp_socket_pool; curr < &tcp_socket_pool[XTCP_MAX_SOCKET_COUNT]; curr++) {
        // 【优化点2】：只检查“活跃”的 Socket
        // 如果 curr 是 FREE 的，即使它残留的 local_port 也是这个值，也不算冲突。
        if (curr != socket && curr->state != XTCP_STATE_FREE && curr->local_port == local_port) {
            return XNET_ERR_BINDED;
        }
    }

    // 2. 绑定
    socket->local_port = local_port;
    return XNET_OK;
}

// 接收到请求后，找到匹配的 socket
// 优先级：已建立连接的五元组匹配 > 监听端口匹配
xtcp_socket_t* xtcp_find_socket(xip_addr_t* remote_ip, uint16_t remote_port, uint16_t local_port) {
    xtcp_socket_t* listen_socket = NULL;

    for (xtcp_socket_t* curr = tcp_socket_pool; curr < &tcp_socket_pool[XTCP_MAX_SOCKET_COUNT]; curr++) {
        // 0. FREE 的直接跳过
        if (curr->state == XTCP_STATE_FREE) {
            continue;
        }

        // 1. 本地端口必须匹配
        if (curr->local_port != local_port) {
            continue;
        }

        // 2. 检查五元组精确匹配（针对 ESTABLISHED, SYN_SENT 等活动连接）
        // 只有远程 IP 和端口都匹配，才是真正的“当前连接”
        // 【逻辑调整】：建议先判断精确匹配，这样逻辑流更顺畅，虽然结果一样。
        if (xip_addr_eq(remote_ip, &curr->remote_ip) && (remote_port == curr->remote_port)) {
            return curr; // 找到唯一活动连接，直接返回
        }

        // 3. 检查 LISTEN 状态 (作为备选)
        // 如果没找到上面的精确匹配，但找到了一个监听该端口的 Server Socket
        if (curr->state == XTCP_STATE_LISTEN) {
            listen_socket = curr;
            // 注意：不要 break，继续往后找。万一后面有一个精确匹配的呢？
            // 比如 socket[0] 是 LISTEN 80，socket[1] 是 ESTABLISHED 80 (conn 1)...
        }
    }

    // 4. 如果没找到活动连接，返回监听 Socket（如果有的话）用来建立新连接
    return listen_socket;
}

xnet_status_t xtcp_listen_socket(xtcp_socket_t* socket) {
    if (socket == NULL) return XNET_ERR_PARAM;

    // 【优化点3】：状态检查
    // 只有处于 CLOSED 状态且已绑定端口的 Socket 才能开始 Listen
    if (socket->state != XTCP_STATE_CLOSED) {
         return XNET_ERR_STATE;
    }
    if (socket->local_port == 0) {
        return XNET_ERR_PARAM; // 必须先 bind
    }

    socket->state = XTCP_STATE_LISTEN;
    return XNET_OK;
}

xnet_status_t xtcp_close_socket(xtcp_socket_t* socket) {
    if (socket == NULL) return XNET_ERR_PARAM;

    // 【优化点4】：资源回收
    // 这是一个强制销毁函数（对应 alloc）。
    // 在完整的 TCP 实现中，这里可能需要判断状态，如果是 ESTABLISHED，可能要发 RST。
    // 但作为资源释放函数，简单置为 FREE 是可以的。
    socket->state = XTCP_STATE_FREE;
    // 建议清空 handler 以防野指针调用
    socket->handler = NULL;
    return XNET_OK;
}