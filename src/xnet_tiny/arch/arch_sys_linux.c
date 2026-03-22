/**
* arch_sys_linux.c
 * Linux 平台下的系统架构抽象实现 (专为 DPDK / 原生 Linux Socket 环境准备)
 */
#include <time.h>         // 引入 POSIX 时间标准库
#include "xnet_tiny.h"    // 引入协议栈通用契约

// 履行通用契约：获取系统单调时间（单位：秒）
xnet_time_t xsys_get_time(void) {
    struct timespec ts;
    // CLOCK_MONOTONIC：自系统启动以来的单调时间，不受任何手动改时或 NTP 同步的影响
    // 这是保证协议栈中 ARP 老化、TCP 超时重传机制不崩溃的核心基石
    clock_gettime(CLOCK_MONOTONIC, &ts);

    return (xnet_time_t)ts.tv_sec;
}