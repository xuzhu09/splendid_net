#include "xnet_dhcp.h"
#include <stdio.h>
#include <string.h>

// 当前 DHCP 的状态
static xnet_dhcp_state_t dhcp_state = DHCP_STATE_DISABLED;
// 记录上一次发包的时间，用于超时重传
static xnet_time_t dhcp_timer;

void xnet_dhcp_init(void) {
    printf(">> [DHCP] Initializing DHCP Client...\n");
    // 启动 DHCP 时，先强行把本机 IP 清零 (0.0.0.0)
    memset(&xnet_local_ip, 0, sizeof(xip_addr_t));

    // 进入 INIT 状态，准备大干一场
    dhcp_state = DHCP_STATE_INIT;
    dhcp_timer = xsys_get_time();
}

void xnet_dhcp_poll(void) {
    // 如果禁用了 DHCP，直接退出
    if (dhcp_state == DHCP_STATE_DISABLED) return;

    // DHCP 的 DORA 状态机
    switch (dhcp_state) {
        case DHCP_STATE_INIT:
            // TODO: 在这里组装并发送 DHCP Discover 广播包
            printf(">> [DHCP] State: INIT. (Ready to send Discover)\n");

            // 发送完后，状态切换为等待回应
            dhcp_state = DHCP_STATE_REQUESTING;
            dhcp_timer = xsys_get_time(); // 记录发包时间
            break;

        case DHCP_STATE_REQUESTING:
            // TODO: 检查是否收到了 Offer 包

            // 简单的超时机制：如果 3 秒都没人理我，重新回到 INIT 状态发广播
            if (xnet_check_tmo(&dhcp_timer, 3)) {
                printf(">> [DHCP] Timeout! No Offer received. Retrying...\n");
                dhcp_state = DHCP_STATE_INIT;
            }
            break;

        case DHCP_STATE_BOUND:
            // 已经拿到 IP 了，这里以后可以处理“租期续约 (Renew)”的逻辑
            // 目前先什么都不做
            break;

        default:
            break;
    }
}