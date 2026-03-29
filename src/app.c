#include <stdio.h>
#include <string.h>
#include "xnet_arp.h"
#include "xnet_tiny.h"
#include "xserver_http.h"
#include "xserver_datetime.h"
#include "xnet_dhcp.h"
#include "xnet_config.h"

int main (void) {
    // 1. 禁用标准输出缓冲，确保 printf 能即时打印到控制台
    setvbuf(stdout, NULL, _IONBF, 0);

    // 是否使用静态IP
    if (XNET_CFG_USE_STATIC_IP) {
        uint8_t ip[]   = CFG_IP_ADDR;
        uint8_t mask[] = CFG_IP_MASK;
        uint8_t gw[]   = CFG_IP_GW;

        // 读取配置文件中的静态IP
        memcpy(xnet_local_ip.addr, ip, 4);
        memcpy(xnet_netmask.addr, mask, 4);
        memcpy(xnet_gateway.addr, gw, 4);
        printf(">> [Network] Static IP Pre-configured: %d.%d.%d.%d\n", ip[0], ip[1], ip[2], ip[3]);
    }

    // 2. 初始化协议栈
    xnet_init();
    printf("xnet stack initialized.\n");

    // 如果是静态 IP，补发无偿 ARP
    if (XNET_CFG_USE_STATIC_IP) {
        xarp_make_request(&xnet_local_ip); // 向网段宣告自己
        printf(">> [Network] Gratuitous ARP sent.\n");
    } else {
        xnet_dhcp_init();
        printf(">> [Network] DHCP Mode Enabled. Searching...\n");
    }

    printf("xnet stack initialized.\n");

    // 3. 启动应用层服务
    // 创建时间服务器，监听端口 13 (TCP)
    if (xserver_datetime_create(13) == XNET_OK) {
        printf("datetime server listening on port 13...\n");
    } else {
        printf("datetime server creation failed!\n");
    }

    // 创建 HTTP 服务器，监听端口 80 (TCP)
    if (xhttp_server_create(80) == XNET_OK) {
        printf("http server listening on port 80...\n");
    } else {
        printf("http server creation failed!\n");
    }

    // 4. 打印本机信息 (方便调试连接)
    printf("------------xnet running at %d.%d.%d.%d, MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
       xnet_local_ip.addr[0], xnet_local_ip.addr[1],
       xnet_local_ip.addr[2], xnet_local_ip.addr[3],
       xnet_local_mac[0], xnet_local_mac[1], xnet_local_mac[2],
       xnet_local_mac[3], xnet_local_mac[4], xnet_local_mac[5]);

    printf("system running, waiting for connections...\n");

    // 5. 超级循环 (The Super Loop)
    while (1) {
        // A. 驱动协议栈 (收包、发包、处理 TCP 状态机)
        // 这是心脏，必须高频调用
        xnet_poll();

        xnet_dhcp_poll();

        // B. 给 HTTP 服务器分配一点 CPU 时间
        // 它会去 socket 队列看一眼：有新连接吗？有就处理，没有就立即返回
        xhttp_server_poll();

        // C. 给时间服务器分配一点 CPU 时间
        // 同样是非阻塞的：看一眼，有连接就发时间，没连接就返回
        xserver_datetime_poll();
    }

    return 0;
}
