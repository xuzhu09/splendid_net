#include "tap_device.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <linux/if_tun.h>

static int tap_fd = -1;
static char tap_name[IFNAMSIZ];

int tap_device_init(const char *dev_name) {
    struct ifreq ifr;
    int err;

    // 1. 打开字符设备
    if ((tap_fd = open("/dev/net/tun", O_RDWR)) < 0) {
        perror("[TAP Driver] open /dev/net/tun failed");
        return TAP_ERR;
    }

    memset(&ifr, 0, sizeof(ifr));
    // 2. IFF_TAP表示二层网络设备, IFF_NO_PI表示不包含附加的包头信息（纯以太网帧）
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;

    if (dev_name && *dev_name) {
        strncpy(ifr.ifr_name, dev_name, IFNAMSIZ);
    } else {
        strncpy(ifr.ifr_name, "tap0", IFNAMSIZ);
    }

    // 3. 注册网络设备
    if ((err = ioctl(tap_fd, TUNSETIFF, (void *) &ifr)) < 0) {
        perror("[TAP Driver] ioctl TUNSETIFF failed (Try running with sudo)");
        close(tap_fd);
        return TAP_ERR;
    }

    strncpy(tap_name, ifr.ifr_name, IFNAMSIZ);

    // 4. 设置为非阻塞模式（契合协议栈的 poll 机制）
    int flags = fcntl(tap_fd, F_GETFL, 0);
    fcntl(tap_fd, F_SETFL, flags | O_NONBLOCK);

    printf(">> [TAP Driver] Initialized on %s\n", tap_name);
    return TAP_OK;
}

int tap_device_send(const void *data, uint16_t len) {
    if (tap_fd < 0) return 0;
    int ret = write(tap_fd, data, len);
    return (ret > 0) ? ret : 0;
}

int tap_device_read(void *buffer, uint16_t max_len) {
    if (tap_fd < 0) return 0;
    int ret = read(tap_fd, buffer, max_len);
    return (ret > 0) ? ret : 0;
}

void tap_device_get_mac(uint8_t *mac_buf) {
    struct ifreq ifr;
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return;

    strncpy(ifr.ifr_name, tap_name, IFNAMSIZ);
    if (ioctl(sock, SIOCGIFHWADDR, &ifr) == 0) {
        memcpy(mac_buf, ifr.ifr_hwaddr.sa_data, 6);
    }
    close(sock);
}