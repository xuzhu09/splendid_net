#include "xnet_dhcp.h"
#include "xnet_udp.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// 当前 DHCP 的状态
static xnet_dhcp_state_t dhcp_state = DHCP_STATE_DISABLED;
// 记录上一次发包的时间，用于超时重传
static xnet_time_t dhcp_timer;

// 我们用一个固定的事务 ID，方便在 Wireshark 里一眼认出它！
#define DHCP_MY_XID 0x11223344

// DHCP 专属的 UDP Socket
static xudp_pcb_t *dhcp_socket = NULL;

static xip_addr_t pending_server_ip;
static xip_addr_t pending_offered_ip;

// 发送 Request 确认函
static void dhcp_send_request(void) {
    uint16_t total_len = sizeof(xnet_dhcp_hdr_t) + 64;
    xnet_packet_t *packet = xnet_alloc_tx_packet(total_len);
    xnet_dhcp_hdr_t *hdr = (xnet_dhcp_hdr_t *)packet->data;
    memset(hdr, 0, sizeof(xnet_dhcp_hdr_t));

    // 基础固定字段和 Discover 几乎一样
    hdr->op = DHCP_OP_REQUEST;
    hdr->htype = 1;
    hdr->hlen = 6;
    hdr->xid = swap_order32(DHCP_MY_XID);
    hdr->flags = swap_order16(0x8000); // 依然要求广播回应
    memcpy(hdr->chaddr, xnet_local_mac, XNET_MAC_ADDR_SIZE);
    hdr->magic_cookie = swap_order32(DHCP_MAGIC_COOKIE);

    uint8_t *options = packet->data + sizeof(xnet_dhcp_hdr_t);
    int opt_idx = 0;

    // Option 53: 消息类型 = Request (3)
    options[opt_idx++] = 53;
    options[opt_idx++] = 1;
    options[opt_idx++] = 3;

    // 🌟 破局点 1：Option 50 (Requested IP) 告诉服务器“我就要这个 IP 了”
    options[opt_idx++] = 50;
    options[opt_idx++] = 4;
    memcpy(&options[opt_idx], pending_offered_ip.addr, 4);
    opt_idx += 4;

    // 🌟 破局点 2：Option 54 (Server Identifier) 告诉服务器“我接受你的分配”
    options[opt_idx++] = 54;
    options[opt_idx++] = 4;
    memcpy(&options[opt_idx], pending_server_ip.addr, 4);
    opt_idx += 4;

    options[opt_idx++] = 255; // End

    packet->len = sizeof(xnet_dhcp_hdr_t) + opt_idx;
    if (packet->len < 300) {
        memset(packet->data + packet->len, 0, 300 - packet->len);
        packet->len = 300;
    }

    printf(">> [DHCP] Sending Request packet for IP: %d.%d.%d.%d...\n",
           pending_offered_ip.addr[0], pending_offered_ip.addr[1],
           pending_offered_ip.addr[2], pending_offered_ip.addr[3]);

    xip_addr_t dest_ip = {{255, 255, 255, 255}};
    if (dhcp_socket) {
        xudp_send_to(dhcp_socket, &dest_ip, 67, packet);
    }
}

// DHCP 回调函数
static xnet_status_t dhcp_udp_handler(xudp_pcb_t *socket, xip_addr_t *src_ip, uint16_t src_port, xnet_packet_t *packet) {
    if (packet->len < sizeof(xnet_dhcp_hdr_t)) return XNET_ERR_PARAM;
    xnet_dhcp_hdr_t *hdr = (xnet_dhcp_hdr_t *)packet->data;

    if (hdr->op != DHCP_OP_REPLY || hdr->magic_cookie != swap_order32(DHCP_MAGIC_COOKIE) || hdr->xid != swap_order32(DHCP_MY_XID)) {
        return XNET_OK;
    }

    // 🌟 状态机判断：这是 Offer 还是 ACK？
    if (dhcp_state == DHCP_STATE_REQUESTING) {
        // 第一阶段：收到了 Offer！
        printf(">> [DHCP] BINGO! Received DHCP Offer!\n");

        // 记住服务器是谁，以及它给的 IP
        memcpy(pending_offered_ip.addr, &hdr->yiaddr, 4);
        memcpy(pending_server_ip.addr, src_ip->addr, 4);

        // 状态切换并发送 Request 索要这个 IP
        dhcp_state = DHCP_STATE_WAITING_ACK;
        dhcp_send_request();
        dhcp_timer = xsys_get_time(); // 重置计时器防超时

    } else if (dhcp_state == DHCP_STATE_WAITING_ACK) {
        // 第二阶段：收到了 ACK！契约达成！
        printf(">> [DHCP] VICTORY! Received DHCP ACK!\n");

        // 1. 保存 IP
        memcpy(xnet_local_ip.addr, &hdr->yiaddr, 4);

        // 🌟 2. 遍历解析 Options，提取掩码和网关
        uint8_t *options = packet->data + sizeof(xnet_dhcp_hdr_t);
        uint8_t *end = packet->data + packet->len; // 包的结尾

        while (options < end && *options != 255) { // 遇到 255 (End) 就停止
            uint8_t opt_type = options[0];

            if (opt_type == 0) { // 遇到 0 (Padding) 直接跳过 1 字节
                options++;
                continue;
            }

            uint8_t opt_len = options[1];
            uint8_t *opt_val = &options[2];

            if (opt_type == 1 && opt_len == 4) {
                // Option 1: 子网掩码 (Subnet Mask)
                // 假设你在 xnet_tiny.c 里定义了 xip_addr_t xnet_netmask;
                memcpy(xnet_netmask.addr, opt_val, 4);
            } else if (opt_type == 3 && opt_len == 4) {
                // Option 3: 路由器/网关 (Router)
                // 假设你在 xnet_tiny.c 里定义了 xip_addr_t xnet_gateway;
                memcpy(xnet_gateway.addr, opt_val, 4);
            }

            // 跳过当前 Option (类型1字节 + 长度1字节 + 数据n字节)
            options += (2 + opt_len);
        }

        dhcp_state = DHCP_STATE_BOUND;

        printf("===================================================\n");
        printf(">> [System] Network configured via DHCP!\n");
        printf(">> [System] IP Address : %d.%d.%d.%d\n", xnet_local_ip.addr[0], xnet_local_ip.addr[1], xnet_local_ip.addr[2], xnet_local_ip.addr[3]);
        printf(">> [System] Subnet Mask: %d.%d.%d.%d\n", xnet_netmask.addr[0], xnet_netmask.addr[1], xnet_netmask.addr[2], xnet_netmask.addr[3]);
        printf(">> [System] Gateway    : %d.%d.%d.%d\n", xnet_gateway.addr[0], xnet_gateway.addr[1], xnet_gateway.addr[2], xnet_gateway.addr[3]);
        printf("===================================================\n");
    }

    return XNET_OK;
}

// 组装并发送 Discover 的核心函数
static void dhcp_send_discover(void) {
    // 1. 申请一个数据包 (大小 = DHCP头 + 一点点 Options 的空间)
    // 注意：这里调用的是你协议栈底层的发包分配函数
    uint16_t total_len = sizeof(xnet_dhcp_hdr_t) + 4; // 先算个大概，后面调整
    xnet_packet_t *packet = xnet_alloc_tx_packet(total_len);

    // 2. 将数据区强制转换为 DHCP 头部指针，并全部清零
    xnet_dhcp_hdr_t *hdr = (xnet_dhcp_hdr_t *)packet->data;
    memset(hdr, 0, sizeof(xnet_dhcp_hdr_t));

    // 3. 填充固定字段 (这就是 C 语言指针操作最爽的地方)
    hdr->op = DHCP_OP_REQUEST;
    hdr->htype = 1;         // 以太网
    hdr->hlen = 6;          // MAC 地址长度
    hdr->xid = swap_order32(DHCP_MY_XID); // 转换字节序 (如果有的话，没有直接赋值 0x11223344)
    hdr->flags = swap_order16(0x8000);    // 告诉服务器：“我没 IP，请务必用全网广播回我！”

    // 4. 填入你的真实 MAC 地址 (非常关键，服务器靠这个认人)
    memcpy(hdr->chaddr, xnet_local_mac, XNET_MAC_ADDR_SIZE);

    // 5. 注入灵魂：魔术字
    hdr->magic_cookie = swap_order32(DHCP_MAGIC_COOKIE);

    // 6. 徒手拼装变长的 Options (紧跟在 magic_cookie 之后)
    uint8_t *options = packet->data + sizeof(xnet_dhcp_hdr_t);
    int opt_idx = 0;

    // Option 53: DHCP 消息类型 (Message Type) = Discover (1)
    options[opt_idx++] = 53; // Type
    options[opt_idx++] = 1;  // Length
    options[opt_idx++] = 1;  // Value (1 = Discover)

    // Option 255: 结束符 (End) - 告诉服务器选项结束了
    options[opt_idx++] = 255;

    // 7. 修正包的最终真实长度
    packet->len = sizeof(xnet_dhcp_hdr_t) + opt_idx;

    printf(">> [DHCP] Sending Discover packet (XID: 0x%08X, len: %d bytes)...\n",
            DHCP_MY_XID, packet->len);

    xip_addr_t dest_ip = {{255, 255, 255, 255}};

    // 通过刚才绑定的 68 端口，将包发送到 67 端口
    if (dhcp_socket) {
        xudp_send_to(dhcp_socket, &dest_ip, 67, packet);
    }
}

void xnet_dhcp_init(void) {
    printf(">> [DHCP] Initializing DHCP Client...\n");
    memset(&xnet_local_ip, 0, sizeof(xip_addr_t));

    // 申请 Socket 并绑定在 68 端口
    if (dhcp_socket == NULL) {
        dhcp_socket = xudp_alloc_socket(dhcp_udp_handler);
        if (dhcp_socket) {
            xudp_bind_socket(dhcp_socket, 68);
        } else {
            printf(">> [DHCP] Panic: Failed to alloc UDP socket!\n");
        }
    }

    dhcp_state = DHCP_STATE_INIT;
    dhcp_timer = xsys_get_time();
}

void xnet_dhcp_poll(void) {
    // 如果禁用了 DHCP，直接退出
    if (dhcp_state == DHCP_STATE_DISABLED) return;

    // DHCP 的 DORA 状态机
    switch (dhcp_state) {
        case DHCP_STATE_INIT:
            printf(">> [DHCP] State: INIT. (Ready to send Discover)\n");
            dhcp_send_discover();

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

        case DHCP_STATE_WAITING_ACK:
            if (xnet_check_tmo(&dhcp_timer, 3)) { // 如果发了 Request 但服务器没给 ACK
                printf(">> [DHCP] Timeout waiting for ACK. Retrying...\n");
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