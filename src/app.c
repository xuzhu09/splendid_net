#include <stdio.h>

#include "xnet_tiny.h"
#include "xserver_http.h"
#include "xserver_datetime.h"

int main (void) {
    setvbuf(stdout, NULL, _IONBF, 0);  // 禁用缓冲，保证 printf 立刻显示

    xnet_init();

    xserver_datetime_create(13);
    xserver_http_create(80);

    // 打印 IP 和 MAC 地址
    printf("------------xnet running at %d.%d.%d.%d, MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
       xnet_local_ip.addr[0], xnet_local_ip.addr[1],
       xnet_local_ip.addr[2], xnet_local_ip.addr[3],
       xnet_local_mac[0], xnet_local_mac[1], xnet_local_mac[2],
       xnet_local_mac[3], xnet_local_mac[4], xnet_local_mac[5]);

    while (1) {
        xnet_poll();
    }

    return 0;
}