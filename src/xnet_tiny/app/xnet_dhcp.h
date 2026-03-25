#ifndef XNET_DHCP_H
#define XNET_DHCP_H

#include "xnet_tiny.h"

// DHCP 客户端的四个经典状态
typedef enum _xnet_dhcp_state_t {
    DHCP_STATE_DISABLED = 0, // 禁用 DHCP (使用静态 IP)
    DHCP_STATE_INIT,         // 初始化状态 (准备发送 Discover)
    DHCP_STATE_REQUESTING,   // 正在请求 (已发送 Discover，等待 Offer)
    DHCP_STATE_BOUND         // 已绑定 (成功拿到 IP！)
} xnet_dhcp_state_t;

// 初始化 DHCP 模块
void xnet_dhcp_init(void);

// DHCP 轮询状态机 (放入主循环)
void xnet_dhcp_poll(void);

#endif // XNET_DHCP_H