#ifndef XNET_DHCP_H
#define XNET_DHCP_H

#include "xnet_tiny.h"

#define DHCP_OP_REQUEST     1           // 请求包
#define DHCP_OP_REPLY       2           // 应答包
#define DHCP_MAGIC_COOKIE   0x63825363  // 魔术字

#pragma pack(1)
// DHCP 报文固定头部 (总长 236 字节)
typedef struct _xnet_dhcp_hdr_t {
    uint8_t op;             // 报文类型 (1=请求, 2=应答)
    uint8_t htype;          // 硬件类型 (1=以太网)
    uint8_t hlen;           // 硬件地址长度 (以太网为 6)
    uint8_t hops;           // 跳数 (填 0)
    uint32_t xid;           // 事务 ID (极其重要！用来认领自己的包)
    uint16_t secs;          // 已过秒数 (填 0)
    uint16_t flags;         // 标志位 (0x8000 表示要求服务器发广播回应)
    uint32_t ciaddr;        // 客户机当前 IP (填 0)
    uint32_t yiaddr;        // 服务器分配的 IP (Your IP)
    uint32_t siaddr;        // 下一个为客户机分配 IP 的服务器 IP (填 0)
    uint32_t giaddr;        // 中继代理 IP (填 0)
    uint8_t chaddr[16];     // 客户机 MAC 地址 (前 6 个字节填真实 MAC，后面补 0)
    uint8_t sname[64];      // 服务器主机名 (填 0)
    uint8_t file[128];      // 启动文件名 (填 0)
    uint32_t magic_cookie;  // 魔术字，证明这是一个 DHCP 包，不是老古董 BOOTP 包
} xnet_dhcp_hdr_t;
#pragma pack()

// DHCP 客户端的四个经典状态
typedef enum _xnet_dhcp_state_t {
    DHCP_STATE_DISABLED = 0, // 禁用 DHCP (使用静态 IP)
    DHCP_STATE_INIT,         // 初始化状态 (准备发送 Discover)
    DHCP_STATE_REQUESTING,   // 正在请求 (已发送 Discover，等待 Offer)
    DHCP_STATE_WAITING_ACK,  // 已经发了 Request，等待 ACK
    DHCP_STATE_BOUND         // 已绑定 (成功拿到 IP！)
} xnet_dhcp_state_t;

// 初始化 DHCP 模块
void xnet_dhcp_init(void);

// DHCP 轮询状态机 (放入主循环)
void xnet_dhcp_poll(void);

#endif // XNET_DHCP_H