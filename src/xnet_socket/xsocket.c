#include "xsocket.h"
#include "xnet_tcp.h"
#include "xnet_udp.h"
#include "xnet_tiny.h"

#include <string.h>

#define XSOCKET_MAX_NUM            40     // 与TCP的PCB保持一致
#define XSOCKET_BACKLOG            10
#define XSOCKET_READ_DEFAULT_POLLS 2000

// UDP 邮箱大小：IPv4(20)+UDP(8) 后的典型 MTU 1500 -> 1472 payload
#define XSOCKET_UDP_RX_BUF_SIZE    1472

struct _xsocket_t {
    xsocket_type_t type;
    uint8_t is_used;

    union {
        xtcp_pcb_t *tcp;
        xudp_pcb_t *udp;
    } pcb;

    // ===== UDP 小邮箱（单包缓存）=====
    uint8_t    udp_rx_buf[XSOCKET_UDP_RX_BUF_SIZE];
    uint16_t   udp_rx_len;      // 0 表示无数据
    xip_addr_t udp_src_ip;
    uint16_t   udp_src_port;
};

static xsocket_t socket_pool[XSOCKET_MAX_NUM];

// 前置声明：底层 UDP 回调
static xnet_status_t on_udp_recv(xudp_pcb_t *pcb,
                                         xip_addr_t *src_ip,
                                         uint16_t src_port,
                                         xnet_packet_t *packet);

static xsocket_t *socket_new(void) {
    for (int i = 0; i < XSOCKET_MAX_NUM; i++) {
        if (!socket_pool[i].is_used) {
            memset(&socket_pool[i], 0, sizeof(socket_pool[i]));
            socket_pool[i].is_used = 1;
            return &socket_pool[i];
        }
    }
    return NULL;
}

static void socket_free(xsocket_t *s) {
    if (!s) return;
    memset(s, 0, sizeof(*s));
}

// ===== 打开/关闭 =====
XNET_EXPORT xsocket_t *xsocket_open(xsocket_type_t type) {
    xsocket_t *s = socket_new();
    if (!s) return NULL;

    s->type = type;

    if (type == XSOCKET_TYPE_TCP) {
        s->pcb.tcp = xtcp_pcb_new();
        if (!s->pcb.tcp) {
            socket_free(s);
            return NULL;
        }
    } else {
        // UDP：注册内部 handler，用邮箱桥接到 recvfrom
        s->pcb.udp = xudp_pcb_new(on_udp_recv);
        if (!s->pcb.udp) {
            socket_free(s);
            return NULL;
        }
    }

    return s;
}

XNET_EXPORT void xsocket_close(xsocket_t *socket) {
    if (!socket) return;

    if (socket->type == XSOCKET_TYPE_TCP) {
        if (socket->pcb.tcp) {
            xtcp_pcb_close(socket->pcb.tcp);
            socket->pcb.tcp = NULL;
        }
    } else {
        if (socket->pcb.udp) {
            xudp_free_pcb(socket->pcb.udp);
            socket->pcb.udp = NULL;
        }
    }

    socket_free(socket);
}

// ===== bind =====

XNET_EXPORT xnet_status_t xsocket_bind(xsocket_t *socket, uint16_t port) {
    if (!socket) return XNET_ERR_PARAM;

    if (socket->type == XSOCKET_TYPE_TCP) {
        if (!socket->pcb.tcp) return XNET_ERR_PARAM;
        return xtcp_pcb_bind(socket->pcb.tcp, port);
    } else {
        if (!socket->pcb.udp) return XNET_ERR_PARAM;
        return xudp_bind_pcb(socket->pcb.udp, port);
    }
}

// ===== TCP 专用 =====

XNET_EXPORT xnet_status_t xsocket_listen(xsocket_t *socket) {
    if (!socket || socket->type != XSOCKET_TYPE_TCP || !socket->pcb.tcp) {
        return XNET_ERR_STATE;
    }

    return xtcp_pcb_listen(socket->pcb.tcp, XSOCKET_BACKLOG);
}

XNET_EXPORT xsocket_t *xsocket_accept(xsocket_t *socket) {
    if (!socket || socket->type != XSOCKET_TYPE_TCP || !socket->pcb.tcp) {
        return NULL;
    }

    xtcp_pcb_t *pcb = xtcp_accept(socket->pcb.tcp);
    if (!pcb) return NULL;

    xsocket_t *client = socket_new();
    if (!client) {
        // 没有 wrapper 资源，只能关掉 child
        xtcp_pcb_close(pcb);
        return NULL;
    }

    client->type = XSOCKET_TYPE_TCP;
    client->pcb.tcp = pcb;
    return client;
}

XNET_EXPORT int xsocket_write(xsocket_t *socket, const char *data, int len) {
    if (!socket || socket->type != XSOCKET_TYPE_TCP || !socket->pcb.tcp) return -1;
    if (!data || len <= 0) return 0;

    xtcp_pcb_t *pcb = socket->pcb.tcp;
    int sent_total = 0;

    while (len > 0) {
        int curr = xtcp_send(pcb, (uint8_t *)data, (uint16_t)len);
        if (curr < 0) return -1;

        if (curr == 0) {
            xnet_poll();
            continue;
        }

        len -= curr;
        data += curr;
        sent_total += curr;

        xnet_poll();
    }

    return sent_total;
}

static int is_alive_for_read(const xtcp_pcb_t *pcb) {
    if (!pcb) return 0;

    switch (pcb->state) {
        case XTCP_STATE_ESTABLISHED:
        case XTCP_STATE_CLOSE_WAIT:
        case XTCP_STATE_FIN_WAIT_1:
        case XTCP_STATE_FIN_WAIT_2:
            return 1;
        default:
            return 0;
    }
}

XNET_EXPORT int xsocket_try_read(xsocket_t *socket, char *buf, int max_len) {
    if (!socket || socket->type != XSOCKET_TYPE_TCP || !socket->pcb.tcp) return -1;
    if (!buf || max_len <= 0) return 0;

    int n = xtcp_recv(socket->pcb.tcp, (uint8_t *)buf, (uint16_t)max_len);
    if (n > 0) return n;

    return is_alive_for_read(socket->pcb.tcp) ? 0 : -1;
}

XNET_EXPORT int xsocket_read_timeout(xsocket_t *socket, char *buf, int max_len, int max_polls) {
    if (!socket || socket->type != XSOCKET_TYPE_TCP || !socket->pcb.tcp) return -1;
    if (!buf || max_len <= 0) return 0;
    if (max_polls <= 0) max_polls = 1;

    xtcp_pcb_t *pcb = socket->pcb.tcp;

    for (int i = 0; i < max_polls; i++) {
        int n = xtcp_recv(pcb, (uint8_t *)buf, (uint16_t)max_len);
        if (n > 0) return n;

        if (!is_alive_for_read(pcb)) return -1;
        xnet_poll();
    }
    return 0;
}

XNET_EXPORT int xsocket_read(xsocket_t *socket, char *buf, int max_len) {
    return xsocket_read_timeout(socket, buf, max_len, XSOCKET_READ_DEFAULT_POLLS);
}

// ===== UDP 专用 =====

// 底层回调：把收到的 UDP payload 放进对应 wrapper 的邮箱
static xnet_status_t on_udp_recv(xudp_pcb_t *pcb,
                                         xip_addr_t *src_ip,
                                         uint16_t src_port,
                                         xnet_packet_t *packet) {
    // 找到对应的 xsocket wrapper（池子很小，直接遍历）
    xsocket_t *s = NULL;
    for (int i = 0; i < XSOCKET_MAX_NUM; i++) {
        if (socket_pool[i].is_used &&
            socket_pool[i].type == XSOCKET_TYPE_UDP &&
            socket_pool[i].pcb.udp == pcb) {
            s = &socket_pool[i];
            break;
        }
    }
    if (!s) return XNET_ERR_PARAM;

    // 邮箱满就丢（UDP 正常行为）
    if (s->udp_rx_len != 0) return XNET_OK;

    uint16_t copy_len = packet->len;
    if (copy_len > XSOCKET_UDP_RX_BUF_SIZE) copy_len = XSOCKET_UDP_RX_BUF_SIZE;

    memcpy(s->udp_rx_buf, packet->data, copy_len);
    s->udp_rx_len = copy_len;
    s->udp_src_ip = *src_ip;
    s->udp_src_port = src_port;

    return XNET_OK;
}

XNET_EXPORT int xsocket_sendto(xsocket_t *socket, const char *data, int len,
                   const xip_addr_t *dest_ip, uint16_t dest_port) {
    if (!socket || socket->type != XSOCKET_TYPE_UDP || !socket->pcb.udp) return -1;
    if (!data || len <= 0 || !dest_ip || dest_port == 0) return -1;

    xnet_packet_t *packet = xnet_prepare_tx_packet((uint16_t)len);
    if (!packet) return -1;

    memcpy(packet->data, data, len);

    xnet_status_t r = xudp_send_to(socket->pcb.udp, (xip_addr_t *)dest_ip, dest_port, packet);
    return (r == XNET_OK) ? len : -1;
}

XNET_EXPORT int xsocket_recvfrom(xsocket_t *socket, char *buf, int max_len,
                     xip_addr_t *src_ip, uint16_t *src_port, int max_polls) {
    if (!socket || socket->type != XSOCKET_TYPE_UDP || !socket->pcb.udp) return -1;
    if (!buf || max_len <= 0) return -1;
    if (max_polls <= 0) max_polls = 1;

    for (int i = 0; i < max_polls; i++) {
        if (socket->udp_rx_len > 0) {
            int n = (socket->udp_rx_len > (uint16_t)max_len) ? max_len : socket->udp_rx_len;

            memcpy(buf, socket->udp_rx_buf, n);
            if (src_ip) *src_ip = socket->udp_src_ip;
            if (src_port) *src_port = socket->udp_src_port;

            socket->udp_rx_len = 0; // 清空邮箱
            return n;
        }

        // 驱动协议栈收包
        xnet_poll();
    }

    return 0; // 超时
}