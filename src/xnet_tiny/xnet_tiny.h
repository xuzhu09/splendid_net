/**
 * 手写 TCP/IP 协议栈
 */
#ifndef XNET_TINY_H
#define XNET_TINY_H

#include <stdint.h>

/**
 * 收发数据包的最大大小
 * 以太网 6 + 6 + 2
 * 数据 1500
 */
#define XNET_CFG_PACKET_MAX_SIZE        1514

// 以太网包头，使用指针偏移的方式读取，故关闭填充字节
#pragma pack(1)

/**
 * MAC地址长度，6个字节，48位二进制数
 * 通常转换成12位的进制数
 * 1101 0111 0011 1001 0101 1000 1111 0000 1010 1010 1111 0000
 * D7-39-58-F0-AA-F0
 */
#define XNET_MAC_ADDR_SIZE              6

/**
 * 以太网数据帧格式：RFC894
 * 此处仅定义，以太网包头
 */
typedef struct _xether_hdr_t {
    uint8_t dest[XNET_MAC_ADDR_SIZE]; // 目标mac地址，6字节
    uint8_t src[XNET_MAC_ADDR_SIZE]; // 源mac地址，6字节
    uint16_t protocol; // 协议/长度，2字节
} xether_hdr_t;

#pragma pack()

/**
 * 错误码枚举
 */
typedef enum _xnet_err_t {
    XNET_ERR_OK = 0,
    XNET_ERR_IO = -1,
} xnet_err_t;

/**
 * 网络数据包
 */
typedef struct _xnet_packet_t {
    uint16_t size; // 包中有效数据大小（因为并不一定会占满载荷）
    uint8_t *data; // 包的数据起始地址
    uint8_t payload[XNET_CFG_PACKET_MAX_SIZE]; // 载荷字节数组
} xnet_packet_t;

/**
 * 分配一个发送包
 * @param size
 * @return
 */
xnet_packet_t *xnet_alloc_for_send(uint16_t size);

/**
 * 分配一个读取包
 * @param size
 * @return
 */
xnet_packet_t *xnet_alloc_for_read(uint16_t size);

/**
 * 打开驱动
 * @param mac_addr
 * @return
 */
xnet_err_t xnet_driver_open(uint8_t *mac_addr);

/**
 * 发送一个数据包
 * @param packet
 * @return
 */
xnet_err_t xnet_driver_send(xnet_packet_t *packet);

/**
 * 读取一个数据包
 * @param packet
 * @return
 */
xnet_err_t xnet_driver_read(xnet_packet_t **packet);

typedef enum _xnet_protocol_t {
    XNET_PROTOCOL_ARP = 0x0806, // ARP协议
    XNET_PROTOCOL_IP = 0x0800, // IP协议
} xnet_protocol_t;

/**
 * 协议栈的初始化
 */
void xnet_init(void);

/**
 * 轮询数据包
 */
void xnet_poll(void);

#endif // XNET_TINY_H
