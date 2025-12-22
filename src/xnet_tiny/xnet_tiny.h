/**
* src/xnet_tiny/xnet_tiny.h
 * 协议栈控制层 (Control Layer)
 * 负责内存管理、生命周期控制、工具函数声明
 */
#ifndef XNET_TINY_H
#define XNET_TINY_H

#include "xnet_def.h"

// TCP 序列号生成宏 (属于逻辑部分，留在这里)
#define tcp_get_init_seq() ((rand() << 16) + rand())

// 全局唯一的本机 IP 地址
extern const xip_addr_t xnet_local_ip;

// 1. 内存管理与包操作 (Buffer Management)

// 分配一个发送包 (从后往前分配)
xnet_packet_t* xnet_alloc_tx_packet(uint16_t size);

// 分配一个读取包 (从前往后分配)
xnet_packet_t* xnet_alloc_rx_packet(uint16_t size);

// 头部操作工具
void add_header(xnet_packet_t* packet, uint16_t header_size);
void remove_header(xnet_packet_t* packet, uint16_t header_size);
void truncate_packet(xnet_packet_t* packet, uint16_t size);

// 2. 协议栈生命周期 (Lifecycle)

// 协议栈初始化
void xnet_init(void);

// 协议栈轮询
void xnet_poll(void);

/**
 * @brief 获取系统时间
 *
 * 获取自系统启动或程序运行以来的时间（秒），用于处理定时任务（如 ARP 超时、TCP 重传）。
 *
 * @return xnet_time_t 当前时间戳 (秒)
 */
const xnet_time_t xsys_get_time(void);

#endif // XNET_TINY_H