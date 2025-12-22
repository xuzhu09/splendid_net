//
// Created by efairy520 on 2025/12/20.
//

#ifndef XNET_DRIVER_H
#define XNET_DRIVER_H

#include <stdint.h>
#include "xnet_def.h"

// ============================================================================
// 驱动层统一接口契约
// 无论是 Windows (Pcap) 还是 Linux (DPDK)，都必须实现以下接口
// ============================================================================

/**
 * @brief 初始化网络驱动
 *
 * 该函数负责打开物理网卡（Pcap 或 DPDK端口），分配必要的资源（内存池、队列等）。
 *
 * @param mac_addr [OUT] 用于回传网卡的物理 MAC 地址 (6字节)
 * 协议栈后续会使用这个 MAC 地址进行 ARP 和以太网帧封装。
 * @return xnet_status_t
 * - XNET_OK: 初始化成功
 * - 其他值: 初始化失败（通常会导致程序退出）
 */
xnet_status_t xnet_driver_open(uint8_t *mac_addr);

/**
 * @brief 发送数据包
 *
 * 将协议栈封装好的数据包通过网卡发送出去。
 * - 对于 Pcap：调用 pcap_sendpacket
 * - 对于 DPDK：申请 mbuf -> 拷贝数据 -> rte_eth_tx_burst
 *
 * @param packet [IN] 待发送的协议栈数据包结构体
 * 包含数据指针 (packet->data) 和长度 (packet->length)
 * @return xnet_status_t
 * - XNET_OK: 发送成功
 * - XNET_ERR_IO: 发送失败 (如链路忙、队列满等)
 */
xnet_status_t xnet_driver_send(xnet_packet_t *packet);

/**
 * @brief 读取数据包 (轮询模式)
 *
 * 尝试从网卡接收一个数据包。该函数通常在主循环中被反复调用。
 *
 * @param packet [OUT] 二级指针，用于返回接收到的数据包。
 * 如果在 Pcap/DPDK 中收到了数据，驱动层需要调用 xnet_alloc_rx_packet
 * 分配内存，并将数据拷贝进去，最后让 *packet 指向它。
 * @return xnet_status_t
 * - XNET_OK: 成功读取到一个数据包
 * - XNET_ERR_IO: 当前没有数据包 (这不是错误，只是表示没收到数据)
 */
xnet_status_t xnet_driver_read(xnet_packet_t **packet);

#endif // XNET_DRIVER_H
