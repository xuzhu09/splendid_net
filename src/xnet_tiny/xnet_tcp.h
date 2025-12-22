//
// Created by efairy520 on 2025/12/9.
//

#ifndef XNET_TCP_H
#define XNET_TCP_H

#include "xnet_def.h"

// 1. TCP PCB 最大数量
#define XTCP_PCB_MAX_NUM   40
#define XTCP_CFG_RTX_BUF_SIZE 2048
#define XTCP_FLAG_FIN    (1 << 0)
#define XTCP_FLAG_SYN    (1 << 1)
#define XTCP_FLAG_RST    (1 << 2)
#define XTCP_FLAG_ACK    (1 << 4)

#define XTCP_KIND_END       0
#define XTCP_KIND_MSS       2
#define XTCP_MSS_DEFAULT    1460
#define XTCP_WIN_DEFAULT    65535
#define XTCP_DATA_MAX_SIZE (XNET_CFG_PACKET_MAX_SIZE - sizeof(xether_hdr_t) - sizeof(xip_hdr_t) - sizeof(xtcp_hdr_t))

#pragma pack(1)
// TCP头部 20个字节（可能还有12字节的选项数据）
typedef struct _xtcp_hdr_t {
    uint16_t src_port;
    uint16_t dest_port;
    uint32_t seq;
    uint32_t ack;
    union {
        uint16_t all;
        struct {
            uint16_t flags : 6;       // 低6位
            uint16_t reserved : 6;    // 中间6位
            uint16_t hdr_len : 4;     // 高4位 （乘以4）
        };
    }hdr_flags;

    uint16_t window;
    uint16_t checksum;
    uint16_t urgent_ptr;
}xtcp_hdr_t;
#pragma pack()

// pcb内部的缓冲区
typedef struct _xtcp_buf_t {
    uint16_t data_count;                // 库存总量(包括已发未确认量)
    uint16_t unacked_count;             // 已发未确认量
    uint16_t front;                     // 可写入位置
    uint16_t tail;                      // 待确认位置
    uint16_t next;                      // 即将发送位置
    uint8_t data[XTCP_CFG_RTX_BUF_SIZE];// 缓冲区
} xtcp_buf_t;

// 2. TCP 生命周期状态
typedef enum _xtcp_state_e {
    XTCP_STATE_FREE,
    XTCP_STATE_CLOSED,
    XTCP_STATE_LISTEN,
    XTCP_STATE_SYN_RECVD,
    XTCP_STATE_ESTABLISHED,
    XTCP_STATE_FIN_WAIT_1,
    XTCP_STATE_FIN_WAIT_2,
    XTCP_STATE_CLOSING,
    XTCP_STATE_TIMED_WAIT,
    XTCP_STATE_CLOSE_WAIT,
    XTCP_STATE_LAST_ACK,
} xtcp_state_t;

// 3. 事件类型
typedef enum _xtcp_event_e {
    XTCP_EVENT_CONNECTED,       // 连接成功
    XTCP_EVENT_DATA_RECEIVED,   // 收到数据
    XTCP_EVENT_CLOSED,          // 连接断开
    XTCP_EVENT_ABORTED,         // 连接异常终止 (RST)
} xtcp_event_t;

// 4. TCP 控制块
typedef struct _xtcp_pcb_t xtcp_pcb_t;

// TCP 事件回调函数指针（接口）
typedef xnet_status_t (*xtcp_event_handler_t) (xtcp_pcb_t* pcb, xtcp_event_t event);

// 5. TCP PCB 结构体
struct _xtcp_pcb_t {
    xtcp_state_t           state;
    uint16_t               local_port;
    uint16_t               remote_port;
    xip_addr_t             remote_ip;
    uint32_t               next_seq;
    uint32_t               unacked_seq;
    uint32_t               ack;
    uint16_t               remote_mss;
    uint16_t               remote_win;
    xtcp_event_handler_t   event_cb;
    xtcp_buf_t             tx_buf;
    xtcp_buf_t             rx_buf;
};

void xtcp_init(void);
void xtcp_in(xip_addr_t* remote_ip, xnet_packet_t* packet);

xtcp_pcb_t* xtcp_pcb_new(xtcp_event_handler_t handler);
xnet_status_t xtcp_pcb_bind(xtcp_pcb_t* pcb, uint16_t local_port);
xtcp_pcb_t* xtcp_pcb_find(xip_addr_t* remote_ip, uint16_t remote_port, uint16_t local_port);
xnet_status_t xtcp_pcb_listen(xtcp_pcb_t* pcb);
xnet_status_t xtcp_pcb_close(xtcp_pcb_t* pcb);

int xtcp_write(xtcp_pcb_t* pcb, uint8_t* data, uint16_t size);
int xtcp_read(xtcp_pcb_t* pcb, uint8_t* data, uint16_t size);


#endif //XNET_TCP_H