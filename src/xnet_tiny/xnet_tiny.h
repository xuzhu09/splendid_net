/**
 * TCP/IP 协议栈头文件
 */
#ifndef XNET_TINY_H
#define XNET_TINY_H

#include <stdint.h>

#define swap_order16(v)   ((((v) & 0xFF) << 8) | (((v) >> 8) & 0xFF)) // 大小端转换
#define swap_order32(v) ( \
/* 1. 把最后的积木 D 移到最前面 (左移 24) */ \
(((v) & 0xFF) << 24) | \
\
/* 2. 把倒数第二的积木 C 移到第二个位置 (左移 16) */ \
((((v) >> 8) & 0xFF) << 16) | \
\
/* 3. 把第二个积木 B 移到倒数第二的位置 (左移 8) */ \
((((v) >> 16) & 0xFF) << 8) | \
\
/* 4. 把最前面的积木 A 移到最后面 (右移 24) */ \
(((v) >> 24) & 0xFF) \
)
#define XNET_CFG_PACKET_MAX_SIZE        1514                // 收发数据包的最大大小 1500+6+6+2
#ifdef _WIN32
    // Windows 协议栈 IP
    #define XNET_CFG_DEFAULT_IP  {192, 168, 254, 2}
#else
    // Linux 协议栈 IP
    #define XNET_CFG_DEFAULT_IP  {192, 168, 56, 200}
#endif
#define xip_addr_eq(a, b)  (memcmp((a), (b), XNET_IPV4_ADDR_SIZE) == 0)
#define XNET_IPV4_ADDR_SIZE             4                   // IP地址长度
#define XNET_MAC_ADDR_SIZE              6                   // MAC地址长度
#ifndef min
    #define min(a, b)           ((a) > (b) ? (b) : (a))
#endif
#define tcp_get_init_seq() ((rand() << 16) + rand())

// 错误码枚举
typedef enum _xnet_status_t {
    XNET_OK = 0,
    XNET_ERR_IO = -1,
    XNET_ERR_NONE = -2,
    XNET_ERR_BINDED = -3,
    XNET_ERR_PARAM = -4,
    XNET_ERR_STATE = -5,
} xnet_status_t;

// 网络数据包，大端，所见即所得
typedef struct _xnet_packet_t {
    uint16_t length;                                // 包中有效数据大小（因为并不一定会占满缓冲区）
    uint8_t* data;                                  // 包的数据起始地址
    uint8_t buffer[XNET_CFG_PACKET_MAX_SIZE];       // 缓冲区
} xnet_packet_t;

typedef uint32_t xnet_time_t;           // 时间类型
const xnet_time_t xsys_get_time(void);

// 分配一个发送包
xnet_packet_t* xnet_alloc_tx_packet(uint16_t size);

// 分配一个读取包
xnet_packet_t* xnet_alloc_rx_packet(uint16_t size);

// 打开驱动
xnet_status_t xnet_driver_open(uint8_t* mac_addr);

// 通过驱动发送数据包
xnet_status_t xnet_driver_send(xnet_packet_t* packet);

// 通过驱动读取数据包
xnet_status_t xnet_driver_read(xnet_packet_t** packet);

void add_header(xnet_packet_t* packet, uint16_t header_size);
void remove_header(xnet_packet_t* packet, uint16_t header_size);
void truncate_packet(xnet_packet_t* packet, uint16_t size);

typedef enum _xnet_protocol_t {
    XNET_PROTOCOL_IP = 0x0800,  // IP协议
    XNET_PROTOCOL_ARP = 0x0806, // ARP协议
    XNET_PROTOCOL_ICMP = 1,     // IP协议
    XNET_PROTOCOL_TCP = 6,      // TCP协议
    XNET_PROTOCOL_UDP = 17,     // UDP协议
} xnet_protocol_t;

// IP地址
typedef struct _xip_addr_t {
    uint8_t addr[XNET_IPV4_ADDR_SIZE]; // 以字节形式存储的ip
} xip_addr_t;

extern const xip_addr_t xnet_local_ip; // 协议栈的IP地址

// 协议栈初始化
void xnet_init(void);

// 协议栈轮询
void xnet_poll(void);

#endif // XNET_TINY_H
