/**
 * src/xnet_tiny/xnet_def.h
 * 协议栈公共定义层 (Contract Layer)
 * 只定义数据结构、枚举、宏，不包含函数逻辑声明
 */
#ifndef XNET_DEF_H
#define XNET_DEF_H

#include <stdint.h>

// 1. 基础配置与宏工具
#define XNET_CFG_PACKET_MAX_SIZE        1514        // 收发数据包的最大大小 1500+6+6+2
#define XNET_IPV4_ADDR_SIZE             4           // IP地址长度
#define XNET_MAC_ADDR_SIZE              6           // MAC地址长度

// 大小端转换
#define swap_order16(v)   ((((v) & 0xFF) << 8) | (((v) >> 8) & 0xFF))
#define swap_order32(v) ( \
    (((v) & 0xFF) << 24) | \
    ((((v) >> 8) & 0xFF) << 16) | \
    ((((v) >> 16) & 0xFF) << 8) | \
    (((v) >> 24) & 0xFF) \
)

#ifndef min
    #define min(a, b)           ((a) > (b) ? (b) : (a))
#endif

// IP 地址比较工具
#define xip_addr_eq(a, b)  (memcmp((a), (b), XNET_IPV4_ADDR_SIZE) == 0)

// 默认 IP 配置
#ifdef _WIN32
    #define XNET_CFG_DEFAULT_IP  {192, 168, 254, 2}
#else
    #define XNET_CFG_DEFAULT_IP  {192, 168, 56, 200}
#endif

// 2. 核心枚举与类型

// 错误码枚举
typedef enum _xnet_status_t {
    XNET_OK = 0,
    XNET_ERR_IO = -1,
    XNET_ERR_NONE = -2,
    XNET_ERR_BINDED = -3,
    XNET_ERR_PARAM = -4,
    XNET_ERR_STATE = -5,
} xnet_status_t;

// 网络层协议类型
typedef enum _xnet_protocol_t {
    XNET_PROTOCOL_IP = 0x0800,  // IP协议
    XNET_PROTOCOL_ARP = 0x0806, // ARP协议
    XNET_PROTOCOL_ICMP = 1,     // ICMP协议
    XNET_PROTOCOL_TCP = 6,      // TCP协议
    XNET_PROTOCOL_UDP = 17,     // UDP协议
} xnet_protocol_t;

// 时间类型
typedef uint32_t xnet_time_t;

// IP地址结构
typedef struct _xip_addr_t {
    uint8_t addr[XNET_IPV4_ADDR_SIZE]; // 以字节形式存储的ip
} xip_addr_t;

// 3. 核心数据包结构 (最重要的结构体)
// 网络数据包，大端，所见即所得
typedef struct _xnet_packet_t {
    uint16_t length;                                // 包中有效数据大小
    uint8_t* data;                                  // 包的数据起始地址 (动态变动)
    uint8_t buffer[XNET_CFG_PACKET_MAX_SIZE];       // 物理缓冲区
} xnet_packet_t;

#endif // XNET_DEF_H