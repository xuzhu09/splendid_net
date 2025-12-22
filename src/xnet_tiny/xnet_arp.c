//
// Created by efairy520 on 2025/10/21.
//
#include "xnet_arp.h"
#include "string.h"
#include "xnet_driver.h"
#include "xnet_ethernet.h"

#define XARP_ENTRY_FREE		            0                   // 空闲
#define XARP_ENTRY_OK		            1                   // 就绪
#define XARP_ENTRY_RESOLVING	        2                   // ARP表项正在解析
#define XARP_TIMER_PERIOD               1                   // ARP扫描周期，1秒

#define XARP_CFG_ENTRY_OK_TMO	        10                  // ARP表项OK超时时间
#define XARP_CFG_ENTRY_RESOLVING_TMO	5                   // ARP表项PENDING超时时间
#define XARP_CFG_MAX_RETRIES		    3                   // ARP表挂起时重试查询次数

// ARP表项
typedef struct _xarp_entry_t {
    xip_addr_t ipaddr;                                      // ip地址
    uint8_t macaddr[XNET_MAC_ADDR_SIZE];                    // mac地址
    uint8_t state;                                          // 状态位
    uint16_t ttl;                                           // 剩余时间
    uint8_t retry_cnt;                                      // 剩余重试次数
} xarp_entry_t;

static xarp_entry_t arp_entry;                              // ARP表项
static xnet_time_t arp_last_time;                           // ARP定时器，记录上一次扫描的时间

void xarp_init(void) {
    arp_entry.state = XARP_ENTRY_FREE;

    // 初始化ARP上一次扫描时间，为当前时间
    xnet_check_tmo(&arp_last_time, 0);
}

// 轮询ARP表项是否超时，超时则重新请求
void xarp_poll(void) {
    // 每隔 PERIOD（1秒） 执行一次
    if (xnet_check_tmo(&arp_last_time, XARP_TIMER_PERIOD)) {
        switch (arp_entry.state) {
            // 对方IP没有响应，才会进到这里
            case XARP_ENTRY_RESOLVING:
                // 每次进来，都过了PERIOD，所以--
                if ((arp_entry.ttl-=XARP_TIMER_PERIOD) <= 0) {     // PENDING超时，准备重试
                    if (arp_entry.retry_cnt-- == 0) { // 重试次数用完，回收
                        arp_entry.state = XARP_ENTRY_FREE;
                    } else {    // 重试次数没有用完，开始重试
                        xarp_make_request(&arp_entry.ipaddr);
                        arp_entry.state = XARP_ENTRY_RESOLVING;
                        arp_entry.ttl = XARP_CFG_ENTRY_RESOLVING_TMO;
                    }
                }
                break;
            case XARP_ENTRY_OK:
                // 每次进来，都过了PERIOD，所以--
                if ((arp_entry.ttl-=XARP_TIMER_PERIOD) <= 0) {     // OK超时，重新请求
                    xarp_make_request(&arp_entry.ipaddr); // 想要测试，需要把虚拟机网络关闭，否则一直ok
                    arp_entry.state = XARP_ENTRY_RESOLVING;
                    arp_entry.ttl = XARP_CFG_ENTRY_RESOLVING_TMO;
                }
                break;
            case XARP_ENTRY_FREE:
                // ARP协议初始化后，默认是FREE状态
                break;

        }
    }
}

/**
 * 更新ARP表项
 * @param src_ip 源IP地址
 * @param mac_addr 对应的mac地址
 */
void update_arp_entry(uint8_t* src_ip, uint8_t* mac_addr) {
    memcpy(arp_entry.ipaddr.addr, src_ip, XNET_IPV4_ADDR_SIZE);
    memcpy(arp_entry.macaddr, mac_addr, XNET_MAC_ADDR_SIZE);
    arp_entry.state = XARP_ENTRY_OK;
    arp_entry.ttl = XARP_CFG_ENTRY_OK_TMO;
    arp_entry.retry_cnt = XARP_CFG_MAX_RETRIES;
}

/**
 * 检查是否超时
 * @param last_time 前一时间
 * @param gap_time 预期超时时间，值为0时，表示获取当前时间
 * @return 0 - 未超时，1-超时
 */
int xnet_check_tmo(xnet_time_t* last_time, uint32_t gap_time) {
    xnet_time_t curr_time = xsys_get_time();
    if (gap_time == 0) {         // sec == 0 ,将 *time 设置成当前时间
        *last_time = curr_time;
        return 0;
    }

    if (curr_time - *last_time >= gap_time) {   // sec != 0，检查超时
        *last_time = curr_time;
        return 1;
    }
    return 0;
}

/**
 * 解析指定的IP地址，如果不在ARP表项中，则发送ARP请求
 * @param ipaddr 查找的ip地址
 * @param mac_addr 返回的mac地址存储区
 * @return XNET_ERR_OK 查找成功，XNET_ERR_NONE 查找失败
 */
xnet_status_t xarp_resolve(const xip_addr_t* ipaddr, uint8_t** mac_addr) {
    if ((arp_entry.state == XARP_ENTRY_OK) && xip_addr_eq(ipaddr->addr, arp_entry.ipaddr.addr)) {
        *mac_addr = arp_entry.macaddr;
        return XNET_OK;
    }

    // 如果已经在解析这个IP了，就不要重置 retry_cnt，避免无限重置
    if (arp_entry.state != XARP_ENTRY_RESOLVING || !xip_addr_eq(ipaddr->addr, arp_entry.ipaddr.addr)) {
        memcpy(arp_entry.ipaddr.addr, ipaddr->addr, XNET_IPV4_ADDR_SIZE);
        arp_entry.state = XARP_ENTRY_RESOLVING;
        arp_entry.ttl = XARP_CFG_ENTRY_RESOLVING_TMO; // 5秒
        arp_entry.retry_cnt = XARP_CFG_MAX_RETRIES;   // 3次

        // 发送第一次请求
        xarp_make_request(ipaddr);
    }

    return XNET_ERR_NONE;
}