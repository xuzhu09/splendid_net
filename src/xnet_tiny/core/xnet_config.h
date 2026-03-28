// 文件位置: src/xnet_tiny/core/xnet_config.h
#ifndef XNET_CONFIG_H
#define XNET_CONFIG_H

// ==========================================
// 1. 静态 IP 总开关 (1: 启用静态 IP, 0: 启用 DHCP)
// 未来移植 STM32 时，可以轻松改为读取 Flash 标志位
// ==========================================
#define XNET_CFG_USE_STATIC_IP 0

// ==========================================
// 2. 根据 CMake 注入的环境宏，自动匹配 IP 网段
// ==========================================
#if defined(ENV_DEBIAN_TAP)
    #define CFG_IP_ADDR   {192, 168, 66, 201}
    #define CFG_IP_MASK   {255, 255, 255, 0}
    #define CFG_IP_GW     {192, 168, 66, 1}

#elif defined(ENV_WIN_PCAP)
    #define CFG_IP_ADDR   {192, 168, 254, 2}
    #define CFG_IP_MASK   {255, 255, 255, 0}
    #define CFG_IP_GW     {192, 168, 254, 1}   // PCAP环境特殊处理，根据网关选择网卡

#elif defined(ENV_UBUNTU_DPDK)
    #define CFG_IP_ADDR   {192, 168, 56, 200}
    #define CFG_IP_MASK   {255, 255, 255, 0}
    #define CFG_IP_GW     {192, 168, 56, 1}

#else
    // 兜底防错机制
    #define CFG_IP_ADDR   {0, 0, 0, 0}
    #define CFG_IP_MASK   {0, 0, 0, 0}
    #define CFG_IP_GW     {0, 0, 0, 0}
#endif

#endif // XNET_CONFIG_H