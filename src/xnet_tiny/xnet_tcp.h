//
// Created by efairy520 on 2025/12/9.
//

#ifndef XNET_TCP_H
#define XNET_TCP_H

#include "xnet_tiny.h"

// 1. 配置宏：名字更具体，表明是 Socket 的最大数量
#define XTCP_MAX_SOCKET_COUNT   40

// 2. Socket 生命周期状态
typedef enum _xtcp_socket_state_t {
    XTCP_STATE_FREE,    // 空闲状态
    XTCP_STATE_USED,        // 已分配/使用中 (后续会扩展为 CLOSED, LISTEN, ESTABLISHED 等协议状态)
} xtcp_socket_state_t;

// 3. 事件类型
typedef enum _xtcp_event_type_t {
    XTCP_EVENT_CONNECTED,       // 连接成功
    XTCP_EVENT_DATA_RECEIVED,   // 收到数据
    XTCP_EVENT_CLOSED,          // 连接断开
    XTCP_EVENT_ABORTED,         // 连接异常终止 (RST)
} xtcp_event_type_t;

// 4. TCP 的通信端点
typedef struct _xtcp_socket_t xtcp_socket_t;

// TCP 事件回调函数指针（接口）
typedef xnet_status_t (*xtcp_event_handler_t)(xtcp_socket_t *socket, xtcp_event_type_t event);

// 5. TCP Socket 结构体
struct _xtcp_socket_t {
    xtcp_socket_state_t    state;       // Socket 的生命周期/协议状态

    // 标识：端口号 (TCP 必须保存端口信息)
    uint16_t               local_port;
    uint16_t               remote_port;
    xip_addr_t             remote_ip;

    // 回调函数
    xtcp_event_handler_t   handler;
};

void xtcp_init(void);
xtcp_socket_t* xtcp_alloc_socket(void);
void xtcp_free_socket(xtcp_socket_t* socket);


#endif //XNET_TCP_H