//
// Created by efairy520 on 2025/12/9.
//

#ifndef XNET_TCP_H
#define XNET_TCP_H

#include "xnet_def.h"

#define XTCP_CFG_RTX_BUF_SIZE 2048          // TCP收发通用缓冲区大小

// 1. 完整定义缓冲区结构体
typedef struct _xtcp_buf_t {
    uint16_t write_idx;                     // 写入位置
    uint16_t ack_idx;                       // 确认位置
    uint16_t send_idx;                      // 发送位置
    uint8_t  data[XTCP_CFG_RTX_BUF_SIZE];   // 缓冲区实体
} xtcp_buf_t;

// 2. TCP 生命周期状态机 (RFC 793 标准状态)
typedef enum _xtcp_state_e {
    XTCP_STATE_FREE,                        // 空闲状态 (控制块未被分配)
    XTCP_STATE_CLOSED,                      // 初始/关闭状态
    XTCP_STATE_LISTEN,                      // 监听状态，等待被动连接
    XTCP_STATE_SYN_RECVD,                   // 收到 SYN 并发送了 SYN+ACK，等待最终 ACK
    XTCP_STATE_ESTABLISHED,                 // 连接已建立，数据传输阶段
    XTCP_STATE_FIN_WAIT_1,                  // 主动关闭：已发送 FIN，等待对方的 ACK
    XTCP_STATE_FIN_WAIT_2,                  // 主动关闭：已收到 ACK，等待对方发送 FIN
    XTCP_STATE_CLOSING,                     // 双方同时关闭：发送了 FIN，也收到了 FIN，等待最后的 ACK
    XTCP_STATE_TIMED_WAIT,                  // 等待 2MSL 以确保对方收到最后的 ACK (TIME_WAIT)
    XTCP_STATE_CLOSE_WAIT,                  // 被动关闭：收到 FIN，等待本地应用层调用 close()
    XTCP_STATE_LAST_ACK,                    // 被动关闭：已发送 FIN，等待最后的 ACK
} xtcp_state_t;

// 3. 事件类型 (向应用层抛出的网络事件)
typedef enum _xtcp_event_e {
    XTCP_EVENT_CONNECTED,                   // 连接成功建立 (三次握手完成)
    XTCP_EVENT_DATA_RECEIVED,               // 收到远端数据，可调用 recv 读取
    XTCP_EVENT_SENT,                        // 数据已被远端 ACK 确认，本地发送缓冲区已腾出空间
    XTCP_EVENT_CLOSED,                      // 正常连接断开 (四次挥手完成)
    XTCP_EVENT_ABORTED,                     // 连接异常终止 (收到 RST 报文等致命错误)
} xtcp_event_t;

// 4. TCP 控制块前置声明
typedef struct _xtcp_pcb_t xtcp_pcb_t;

// 6. TCP PCB (Protocol Control Block) 核心结构体
struct _xtcp_pcb_t {
    // ===== 基础属性(IP与端口) =====
    xtcp_state_t           state;           // TCP 状态机当前状态
    uint16_t               local_port;      // 本地端口号
    uint16_t               remote_port;     // 远端端口号
    xip_addr_t             remote_ip;       // 远端 IP 地址

    // ===== 滑动窗口 =====
    uint32_t               snd_nxt;         // 发送窗口：下一个将要发送的序列号
    uint32_t               snd_una;         // 发送窗口：最早已发送但尚未被确认的序列号
    uint32_t               rcv_nxt;         // 接收窗口：期望收到的下一个序列号
    uint16_t               remote_mss;      // 远端最大报文段长度 (MSS)
    uint16_t               remote_win;      // 远端接收窗口大小

    // ===== 缓冲区 =====
    xtcp_buf_t             tx_buf;          // 发送缓冲区
    xtcp_buf_t             rx_buf;          // 接收缓冲区

    // ===== 监听与全连接队列 (LwIP-like backlog support) =====
    xtcp_pcb_t            *lpcb;            // 指向父级监听 PCB 的指针
    xtcp_pcb_t            *accept_next;     // 链表指针：全连接队列中的下一个子连接
    xtcp_pcb_t            *accept_head;     // 全连接队列 (已完成三次握手) 头指针
    xtcp_pcb_t            *accept_tail;     // 全连接队列尾指针
    uint8_t                backlog;         // 最大允许的挂起连接数 (全连接队列总容量)
    uint8_t                accept_cnt;      // 当前已就绪但未被 accept 取走的连接数
};

// ===== 协议栈核心处理接口 =====
void           xtcp_init(void);
void           xtcp_in(xip_addr_t *remote_ip, xnet_packet_t *packet);

// ===== 控制块 (PCB) 生命周期与状态管理 =====
xtcp_pcb_t    *xtcp_pcb_new(void);
xnet_status_t  xtcp_pcb_bind(xtcp_pcb_t *pcb, uint16_t local_port);
xtcp_pcb_t    *xtcp_pcb_find(xip_addr_t *remote_ip, uint16_t remote_port, uint16_t local_port);
xnet_status_t  xtcp_pcb_listen(xtcp_pcb_t *pcb, uint8_t backlog);
xnet_status_t  xtcp_pcb_close(xtcp_pcb_t *pcb);

// ===== 数据收发与连接提取 =====
int            xtcp_send(xtcp_pcb_t *pcb, uint8_t *src, uint16_t len);
int            xtcp_recv(xtcp_pcb_t *pcb, uint8_t *dest, uint16_t len);
xtcp_pcb_t    *xtcp_accept(xtcp_pcb_t *lpcb);

#endif //XNET_TCP_H