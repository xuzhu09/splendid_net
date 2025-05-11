/**
 * 手写 TCP/IP 协议栈
 */
#ifndef XNET_TINY_H
#define XNET_TINY_H

#include <stdint.h>

#define XNET_CFG_PACKET_MAX_SIZE        1514                // 收发数据包的最大大小
#define XNET_CFG_NETIF_IP               {192, 168, 254, 2}  // 协议栈的IP地址

// 以太网包头，使用指针偏移的方式读取，故关闭填充字节
#pragma pack(1)

/**
 * IP地址长度
 */
#define XNET_IPV4_ADDR_SIZE             4

/**
 * MAC地址长度
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

#define XARP_HW_ETHER               0x1         // 以太网
#define XARP_REQUEST                0x1         // ARP请求包
#define XARP_REPLY                  0x2         // ARP响应包

/**
 * ARP 包
 */
typedef struct _xarp_packet_t {
    uint16_t hw_type, protocol_type;            // 硬件类型和协议类型
    uint8_t hw_len, protocol_len;               // 硬件地址长 + 协议地址长
    uint16_t opcode;                            // 请求/响应
    uint8_t sender_mac[XNET_MAC_ADDR_SIZE];     // 发送包硬件地址
    uint8_t sender_ip[XNET_IPV4_ADDR_SIZE];     // 发送包协议地址
    uint8_t target_mac[XNET_MAC_ADDR_SIZE];     // 接收方硬件地址
    uint8_t target_ip[XNET_IPV4_ADDR_SIZE];     // 接收方协议地址
}xarp_packet_t;

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
 * IP地址，使用共用体，节省空间
 */
typedef union _xipaddr_t {
    uint8_t array[XNET_IPV4_ADDR_SIZE]; // 以字节形式存储的ip
    uint32_t addr; // 32位的ip地址
} xipaddr_t;

#define XARP_ENTRY_FREE		        0   // 空闲
#define XARP_ENTRY_OK		        1   // 就绪

/**
 * ARP表项
 */
typedef struct _xarp_entry_t {
    xipaddr_t ipaddr; // ip地址
    uint8_t macaddr[XNET_MAC_ADDR_SIZE]; // mac地址
    uint8_t state; // 状态位
    uint16_t tmo; // 当前超时
    uint8_t retry_cnt; // 当前重试次数
} xarp_entry_t;

/**
 * 协议栈的初始化
 */
void xnet_init(void);

/**
 * 轮询数据包
 */
void xnet_poll(void);

#endif // XNET_TINY_H
