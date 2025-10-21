/**
 * 手写 TCP/IP 协议栈
 */
#include <string.h>
#include "xnet_tiny.h"
#include "xnet_ethernet.h"

#define min(a, b)               ((a) > (b) ? (b) : (a))

static xnet_packet_t tx_packet, rx_packet; // 接收与发送缓冲区
static xarp_entry_t arp_entry; // ARP表项
static xnet_time_t arp_last_time; // ARP定时器，记录上一次扫描的时间


/**
 * 检查是否超时
 * @param time 前一时间
 * @param sec 预期超时时间，值为0时，表示获取当前时间
 * @return 0 - 未超时，1-超时
 */
int xnet_check_tmo(xnet_time_t *time, uint32_t sec) {
    xnet_time_t curr = xsys_get_time();
    if (sec == 0) {         // sec == 0 ,将 *time 设置成当前时间
        *time = curr;
        return 0;
    } else if (curr - *time >= sec) {   // sec != 0，检查超时
        *time = curr;
        return 1;
    }
    return 0;
}

/**
 * 为发包添加一个头部
 * @param packet 待处理的数据包
 * @param header_size 增加的头部大小
 */
void add_header(xnet_packet_t *packet, uint16_t header_size) {
    // 指针前移
    packet->data -= header_size;
    packet->size += header_size;
}

/**
 * 为接收向上处理移去头部
 * @param packet 待处理的数据包
 * @param header_size 移去的头部大小
 */
void remove_header(xnet_packet_t *packet, uint16_t header_size) {
    // 指针后移
    packet->data += header_size;
    packet->size -= header_size;
}

/**
 * 将包的长度截断为size大小
 * @param packet 待处理的数据包
 * @param size 最终大小
 */
void truncate_packet(xnet_packet_t *packet, uint16_t size) {
    packet->size = min(packet->size, size);
}

/**
 * 分配一个网络数据包用于发送数据
 * @param size 数据空间大小
 * @return 分配得到的包结构
 */
xnet_packet_t *xnet_alloc_for_send(uint16_t size) {
    // 从tx_packet的后端往前分配，因为前边要预留作为各种协议的头部数据存储空间
    tx_packet.data = tx_packet.payload + XNET_CFG_PACKET_MAX_SIZE - size;
    tx_packet.size = size;
    return &tx_packet;
}

/**
 * 分配一个网络数据包用于读取
 * @param size 数据空间大小
 * @return 分配得到的数据包
 */
xnet_packet_t *xnet_alloc_for_read(uint16_t size) {
    // 从最开始进行分配，用于最底层的网络数据帧读取
    rx_packet.data = rx_packet.payload;
    rx_packet.size = size;
    return &rx_packet;
}

/**
 * 更新ARP表项
 * @param src_ip 源IP地址
 * @param mac_addr 对应的mac地址
 */
void update_arp_entry(uint8_t *src_ip, uint8_t *mac_addr) {
    memcpy(arp_entry.ipaddr.array, src_ip, XNET_IPV4_ADDR_SIZE);
    memcpy(arp_entry.macaddr, mac_addr, XNET_MAC_ADDR_SIZE);
    arp_entry.state = XARP_ENTRY_OK;
    arp_entry.ttl = XARP_CFG_ENTRY_OK_TMO;
    arp_entry.retry_cnt = XARP_CFG_MAX_RETRIES;
}

/**
 * 生成一个ARP响应
 * @param target_ip
 * @param target_mac
 * @param arp_in_packet 接收到的ARP请求包
 * @return 生成结果
 */
xnet_err_t xarp_make_response(uint8_t *target_ip, uint8_t *target_mac) {
    xarp_packet_t *arp_packet;
    xnet_packet_t *packet = xnet_alloc_for_send(sizeof(xarp_packet_t));

    arp_packet = (xarp_packet_t *) packet->data;
    arp_packet->hw_type = swap_order16(XARP_HW_ETHER);
    arp_packet->protocol_type = swap_order16(XNET_PROTOCOL_IP);
    arp_packet->hw_len = XNET_MAC_ADDR_SIZE;
    arp_packet->protocol_len = XNET_IPV4_ADDR_SIZE;
    arp_packet->opcode = swap_order16(XARP_REPLY);
    memcpy(arp_packet->target_mac, target_mac, XNET_MAC_ADDR_SIZE);
    memcpy(arp_packet->target_ip, target_ip, XNET_IPV4_ADDR_SIZE);
    memcpy(arp_packet->sender_mac, netif_mac, XNET_MAC_ADDR_SIZE);
    memcpy(arp_packet->sender_ip, netif_ipaddr.array, XNET_IPV4_ADDR_SIZE);
    return ethernet_out_to(XNET_PROTOCOL_ARP, ether_broadcast, packet);
}


/**
 * 查询ARP表项是否超时，超时则重新请求
 */
static void xarp_poll(void) {
    // 每隔 PERIOD 执行一次
    if (xnet_check_tmo(&arp_last_time, XARP_TIMER_PERIOD)) {
        switch (arp_entry.state) {
            // 对方IP没有响应，才会进到这里
            case XARP_ENTRY_RESOLVING:
                // 每次进来，都过了PERIOD，所以--
                if ((arp_entry.ttl-=XARP_TIMER_PERIOD) <= 0) {     // PENDING超时，准备重试
                    if (arp_entry.retry_cnt-- == 0) { // 重试次数用完，回收
                        arp_entry.state = XARP_ENTRY_FREE;
                        arp_entry.ipaddr.addr = 0;
                    } else {    // 重试次数没有用完，开始重试
                        xarp_make_request(&arp_entry.ipaddr);
                        arp_entry.ttl = XARP_CFG_ENTRY_PENDING_TMO;
                    }
                }
                break;
            case XARP_ENTRY_OK:
                // 每次进来，都过了PERIOD，所以--
                if ((arp_entry.ttl-=XARP_TIMER_PERIOD) <= 0) {     // OK超时，重新请求
                    xarp_make_request(&arp_entry.ipaddr);
                    arp_entry.state = XARP_ENTRY_RESOLVING;
                    arp_entry.ttl = XARP_CFG_ENTRY_PENDING_TMO;
                }
                break;
            case XARP_ENTRY_FREE:
                // 由于目前没有响应无回报的ARP，故默认是FREE状态
                break;

        }
    }
}

static void xarp_init(void) {
    arp_entry.state = XARP_ENTRY_FREE;

    // 设置系统启动时间
    xnet_check_tmo(&arp_last_time, 0);
}

/**
 * 协议栈的初始化
 */
void xnet_init(void) {
    ethernet_init();
    xarp_init();
}

/**
 * 轮询数据包
 */
void xnet_poll(void) {
    ethernet_poll();
    xarp_poll();
}
