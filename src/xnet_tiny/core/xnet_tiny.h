/**
* src/xnet_tiny/xnet_tiny.h
 * 协议栈控制层 (Control Layer)
 * 负责内存管理、生命周期控制、工具函数声明
 */
#ifndef XNET_TINY_H
#define XNET_TINY_H

#include "xnet_def.h"

// 全局唯一的本机 IP 地址
extern xip_addr_t xnet_local_ip;
extern xip_addr_t xnet_netmask;
extern xip_addr_t xnet_gateway;
extern int xnet_cfg_hw_csum;
extern uint8_t xnet_local_mac[XNET_MAC_ADDR_SIZE];

// 1. 内存管理与包操作 (Buffer Management)

// 分配一个发送包 (从后往前分配)
xnet_packet_t *xnet_alloc_tx_packet(uint16_t size);

// 分配一个读取包 (从前往后分配)
xnet_packet_t *xnet_alloc_rx_packet(uint16_t size);

// 头部操作工具
void add_header(xnet_packet_t *packet, uint16_t header_size);
void remove_header(xnet_packet_t *packet, uint16_t header_size);
void truncate_packet(xnet_packet_t *packet, uint16_t new_len);

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
xnet_time_t xsys_get_time(void);

void xsys_init(void);

// 记录当前时间
void xnet_time_record(xnet_time_t *record_time);

// 检查是否超时
int xnet_time_check_tmo(xnet_time_t *last_time, uint32_t period);

#endif // XNET_TINY_H