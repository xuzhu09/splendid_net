//
// Created by efairy520 on 2025/10/21.
//
#include "xnet_arp.h"
#include "xnet_config.h"
#include "string.h"
#include "xnet_ethernet.h"

#define XARP_ENTRY_FREE		                0               // 空闲
#define XARP_ENTRY_OK		                1               // 就绪
#define XARP_ENTRY_RESOLVING	            2               // ARP表项正在解析

// ARP表项
typedef struct _xarp_entry_t {
    xip_addr_t ipaddr;                                      // ip地址
    uint8_t macaddr[XNET_MAC_ADDR_SIZE];                    // mac地址
    uint8_t state;                                          // 状态位
    uint16_t ttl;                                           // 剩余时间
    uint8_t retry_cnt;                                      // 剩余重试次数
} xarp_entry_t;

static xarp_entry_t arp_table[XARP_CFG_TABLE_SIZE];         // ARP表
static xnet_time_t arp_last_time;                           // ARP定时器，记录上一次扫描的时间

/**
 * 内部查找函数：根据目标 IP 地址在 ARP 表中寻找对应的表项
 * @param ipaddr 查找的ip地址
 * @return 找到的表项指针，未找到则返回 NULL
 */
static xarp_entry_t* xarp_find_by_ip(const uint8_t *ipaddr) {
    for (int i = 0; i < XARP_CFG_TABLE_SIZE; i++) {
        if (arp_table[i].state != XARP_ENTRY_FREE && xip_addr_eq(ipaddr, arp_table[i].ipaddr.addr)) {
            return &arp_table[i];
        }
    }
    return NULL;
}

/**
 * 从 ARP 表中寻找一个可用的表项
 * 策略：优先分配空闲项；若表满，则淘汰存活时间最短（最老）的表项（LRU）
 * @return 可用表项的指针
 */
static xarp_entry_t* xarp_get_free_entry(void) {
    // 默认拿第一个当“最老候选人”
    xarp_entry_t *oldest = &arp_table[0];

    for (int i = 0; i < XARP_CFG_TABLE_SIZE; i++) {
        // 只要遇到空座，立刻入座，毫不犹豫
        if (arp_table[i].state == XARP_ENTRY_FREE) {
            return &arp_table[i];
        }
        // 能走到这，说明当前项不是空闲的。顺手更新一下寿命最少的倒霉蛋
        if (arp_table[i].ttl < oldest->ttl) {
            oldest = &arp_table[i];
        }
    }

    // 如果循环结束还没 return，说明没空座了，直接把最老的交出去覆盖掉
    return oldest;
}

/**
 * 更新ARP表项
 * @param src_ip 源IP地址
 * @param mac_addr 对应的mac地址
 */
static void update_arp_entry(uint8_t *src_ip, uint8_t *mac_addr) {
    xarp_entry_t *entry = xarp_find_by_ip(src_ip);

    if (entry == NULL) {
        entry = xarp_get_free_entry();
    }

    memcpy(entry->ipaddr.addr, src_ip, XNET_IPV4_ADDR_SIZE);
    memcpy(entry->macaddr, mac_addr, XNET_MAC_ADDR_SIZE);
    entry->state = XARP_ENTRY_OK;
    entry->ttl = XARP_CFG_ENTRY_OK_TMO;
    entry->retry_cnt = XARP_CFG_MAX_RETRIES;
}

/**
 * 生成一个ARP响应
 * @param target_ip
 * @param target_mac
 * @param arp_in_packet 接收到的ARP请求包
 * @return 生成结果
 */
static xnet_status_t xarp_make_response(uint8_t *target_ip, uint8_t *target_mac) {
    xarp_packet_t *arp_packet;
    xnet_packet_t *packet = xnet_alloc_tx_packet(sizeof(xarp_packet_t));

    arp_packet = (xarp_packet_t*) packet->data;
    arp_packet->hardware_type = swap_order16(XARP_HW_ETHER);
    arp_packet->protocol_type = swap_order16(XNET_PROTOCOL_IP);
    arp_packet->hardware_len = XNET_MAC_ADDR_SIZE;
    arp_packet->protocol_len = XNET_IPV4_ADDR_SIZE;
    arp_packet->opcode = swap_order16(XARP_REPLY);
    memcpy(arp_packet->sender_mac, xnet_local_mac, XNET_MAC_ADDR_SIZE);
    memcpy(arp_packet->sender_ip, xnet_local_ip.addr, XNET_IPV4_ADDR_SIZE);
    memcpy(arp_packet->target_mac, target_mac, XNET_MAC_ADDR_SIZE);
    memcpy(arp_packet->target_ip, target_ip, XNET_IPV4_ADDR_SIZE);
    // 发送ARP响应，单播
    return ethernet_out_to(XNET_PROTOCOL_ARP, target_mac, packet);
}

void xarp_init(void) {
    for (int i = 0; i < XARP_CFG_TABLE_SIZE; i++) {
        arp_table[i].state = XARP_ENTRY_FREE;
    }

    // 初始化ARP上一次扫描时间，为当前时间
    xnet_check_tmo(&arp_last_time, 0);
}

// 轮询ARP表项是否超时，超时则重新请求
void xarp_poll(void) {
    // 每隔 PERIOD（1秒） 执行一次
    if (xnet_check_tmo(&arp_last_time, XARP_CFG_TIMER_PERIOD)) {
        for (int i = 0; i < XARP_CFG_TABLE_SIZE; i++) {
            switch (arp_table[i].state) {
                // 对方IP没有响应，才会进到这里
                case XARP_ENTRY_RESOLVING:
                    // 每次进来，都过了PERIOD，所以--
                    if (arp_table[i].ttl <= XARP_CFG_TIMER_PERIOD) {     // PENDING超时，准备重试
                        if (arp_table[i].retry_cnt-- == 0) { // 重试次数用完，回收
                            arp_table[i].state = XARP_ENTRY_FREE;
                        } else {    // 重试次数没有用完，开始重试
                            xarp_make_request(&arp_table[i].ipaddr);
                            arp_table[i].state = XARP_ENTRY_RESOLVING;
                            arp_table[i].ttl = XARP_CFG_ENTRY_RESOLVING_TMO;
                        }
                    } else {
                        arp_table[i].ttl -= XARP_CFG_TIMER_PERIOD;
                    }
                    break;
                case XARP_ENTRY_OK:
                    // 每次进来，都过了PERIOD，所以--
                    if (arp_table[i].ttl <= XARP_CFG_TIMER_PERIOD) {     // OK超时，重新请求
                        xarp_make_request(&arp_table[i].ipaddr); // 想要测试，需要把虚拟机网络关闭，否则一直ok
                        arp_table[i].state = XARP_ENTRY_RESOLVING;
                        arp_table[i].ttl = XARP_CFG_ENTRY_RESOLVING_TMO;
                        arp_table[i].retry_cnt = XARP_CFG_MAX_RETRIES;
                    } else {
                        arp_table[i].ttl -= XARP_CFG_TIMER_PERIOD;
                    }
                    break;
                case XARP_ENTRY_FREE:
                    // ARP协议初始化后，默认是FREE状态
                    break;

            }
        }
    }
}

/**
 * 解析指定的IP地址，如果不在ARP表项中，则发送ARP请求
 * @param ipaddr 查找的ip地址
 * @param mac_addr 返回的mac地址存储区
 * @return XNET_ERR_OK 查找成功，XNET_ERR_NONE 查找失败
 */
xnet_status_t xarp_resolve(const xip_addr_t *ipaddr, uint8_t **mac_addr) {
    xarp_entry_t *entry = xarp_find_by_ip(ipaddr->addr);

    if (entry != NULL) {
        // 匹配到了arp表项，直接返回 mac 地址
        if (entry->state == XARP_ENTRY_OK) {
            *mac_addr = entry->macaddr;
            return XNET_OK;
        }
        return XNET_ERR_NONE;
    }

    // 没有匹配到arp表项，发送arp请求
    entry = xarp_get_free_entry();
    memcpy(entry->ipaddr.addr, ipaddr->addr, XNET_IPV4_ADDR_SIZE);
    entry->state = XARP_ENTRY_RESOLVING;
    entry->ttl = XARP_CFG_ENTRY_RESOLVING_TMO;
    entry->retry_cnt = XARP_CFG_MAX_RETRIES;

    // 发送第一次请求
    xarp_make_request(ipaddr);

    return XNET_ERR_NONE;
}

/**
 * 构造一个ARP数据包，并通过以太网广播
 * @param target_ipaddr 传入目标IP，或者传自己的IP
 * @return 请求结果
 */
xnet_status_t xarp_make_request(const xip_addr_t *target_ipaddr) {
    // 准备一个发送包
    xarp_packet_t *arp_packet;
    xnet_packet_t *xnet_packet = xnet_alloc_tx_packet(sizeof(xarp_packet_t));

    // 让 arp_packet 指向 data 首地址，配置载荷
    arp_packet = (xarp_packet_t*) xnet_packet->data;
    arp_packet->hardware_type = swap_order16(XARP_HW_ETHER);
    arp_packet->protocol_type = swap_order16(XNET_PROTOCOL_IP);
    arp_packet->hardware_len = XNET_MAC_ADDR_SIZE;
    arp_packet->protocol_len = XNET_IPV4_ADDR_SIZE;
    arp_packet->opcode = swap_order16(XARP_REQUEST);
    memcpy(arp_packet->sender_mac, xnet_local_mac, XNET_MAC_ADDR_SIZE);
    memcpy(arp_packet->sender_ip, xnet_local_ip.addr, XNET_IPV4_ADDR_SIZE);
    memset(arp_packet->target_mac, 0, XNET_MAC_ADDR_SIZE);
    memcpy(arp_packet->target_ip, target_ipaddr->addr, XNET_IPV4_ADDR_SIZE);
    // 发送ARP请求，多播
    return ethernet_out_to(XNET_PROTOCOL_ARP, ether_broadcast_mac, xnet_packet);
}

/**
 * ARP输入处理
 * @param packet 输入的ARP包
 */
void xarp_in(xnet_packet_t *packet) {
    // 如果小于，说明数据错误，直接忽略这个arp请求
    if (packet->len < sizeof(xarp_packet_t)) return;

    // 包的合法性检查
    xarp_packet_t *arp_packet = (xarp_packet_t*) packet->data;
    uint16_t opcode = swap_order16(arp_packet->opcode);
    if ((swap_order16(arp_packet->hardware_type) != XARP_HW_ETHER) ||
        (arp_packet->hardware_len != XNET_MAC_ADDR_SIZE) ||
        (swap_order16(arp_packet->protocol_type) != XNET_PROTOCOL_IP) ||
        (arp_packet->protocol_len != XNET_IPV4_ADDR_SIZE)
        || ((opcode != XARP_REQUEST) && (opcode != XARP_REPLY))) {
        return;
        }

    // 处理无偿ARP
    if (xip_addr_eq(arp_packet->sender_ip, arp_packet->target_ip)) {
        update_arp_entry(arp_packet->sender_ip, arp_packet->sender_mac);
        return;
    }

    // 只处理发给自己的ARP
    if (!xip_addr_eq(xnet_local_ip.addr, arp_packet->target_ip)) {
        return;
    }


    // 根据操作码进行不同的处理
    switch (swap_order16(arp_packet->opcode)) {
        case XARP_REQUEST: // 收到请求，回送响应
            // 在对方机器Ping 自己，然后看wireshark，能看到ARP请求和响应
            // 接下来，很可能对方要与自己通信，所以更新一下
            update_arp_entry(arp_packet->sender_ip, arp_packet->sender_mac);
            xarp_make_response(arp_packet->sender_ip, arp_packet->sender_mac);
            break;
        case XARP_REPLY: // 收到响应，更新自己的表
            update_arp_entry(arp_packet->sender_ip, arp_packet->sender_mac);
            break;
    }

}