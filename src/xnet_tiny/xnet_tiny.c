/**
 * 手写 TCP/IP 协议栈
 */
#include <string.h>
#include "xnet_tiny.h"

#define min(a, b)               ((a) > (b) ? (b) : (a))

static const xipaddr_t netif_ipaddr = XNET_CFG_NETIF_IP; // 协议栈的IP地址
static const uint8_t ether_broadcast[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; // 广播mac地址
static uint8_t netif_mac[XNET_MAC_ADDR_SIZE]; // 协议栈mac地址
static xnet_packet_t tx_packet, rx_packet; // 接收与发送缓冲区
static xarp_entry_t arp_entry; // ARP表项
static xnet_time_t arp_last_time; // ARP定时器，记录上一次扫描的时间

#define swap_order16(v)   ((((v) & 0xFF) << 8) | (((v) >> 8) & 0xFF)) // 大小端转换
#define xipaddr_is_equal_buf(addr, buf)      (memcmp((addr)->array, buf, XNET_IPV4_ADDR_SIZE) == 0)   // 相等比较

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
static void add_header(xnet_packet_t *packet, uint16_t header_size) {
    // 指针前移
    packet->data -= header_size;
    packet->size += header_size;
}

/**
 * 为接收向上处理移去头部
 * @param packet 待处理的数据包
 * @param header_size 移去的头部大小
 */
static void remove_header(xnet_packet_t *packet, uint16_t header_size) {
    // 指针后移
    packet->data += header_size;
    packet->size -= header_size;
}

/**
 * 将包的长度截断为size大小
 * @param packet 待处理的数据包
 * @param size 最终大小
 */
static void truncate_packet(xnet_packet_t *packet, uint16_t size) {
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
 * 发送一个以太网数据帧
 * @param protocol 上层数据协议，IP或ARP
 * @param mac_addr 目标网卡的mac地址
 * @param packet 待发送的数据包
 * @return 发送结果
 */
static xnet_err_t ethernet_out_to(xnet_protocol_t protocol, const uint8_t *mac_addr, xnet_packet_t *packet) {
    xether_hdr_t *ether_hdr;

    // 添加以太网头部，指针前移
    // 传入的ether_hdr并未携带任何数据
    add_header(packet, sizeof(xether_hdr_t));
    ether_hdr = (xether_hdr_t *) packet->data;
    memcpy(ether_hdr->dest, mac_addr, XNET_MAC_ADDR_SIZE);
    memcpy(ether_hdr->src, netif_mac, XNET_MAC_ADDR_SIZE);
    ether_hdr->protocol = swap_order16(protocol);

    // 数据发送
    return xnet_driver_send(packet);
}

/**
 * 产生一个ARP请求，请求网络指定ip地址的机器发回一个ARP响应
 * @param target_ipaddr 请求的IP地址
 * @return 请求结果
 */
static xnet_err_t xarp_make_request(const xipaddr_t *target_ipaddr) {
    // 新建 arp_packet 和 packet
    xarp_packet_t *arp_packet;
    xnet_packet_t *xnet_packet = xnet_alloc_for_send(sizeof(xarp_packet_t));

    // 让 arp_packet 指向 data 首地址，配置载荷
    arp_packet = (xarp_packet_t *) xnet_packet->data;
    arp_packet->hw_type = swap_order16(XARP_HW_ETHER);
    arp_packet->protocol_type = swap_order16(XNET_PROTOCOL_IP);
    arp_packet->hw_len = XNET_MAC_ADDR_SIZE;
    arp_packet->protocol_len = XNET_IPV4_ADDR_SIZE;
    arp_packet->opcode = swap_order16(XARP_REQUEST);
    memcpy(arp_packet->sender_mac, netif_mac, XNET_MAC_ADDR_SIZE);
    memcpy(arp_packet->sender_ip, netif_ipaddr.array, XNET_IPV4_ADDR_SIZE);
    memset(arp_packet->target_mac, 0, XNET_MAC_ADDR_SIZE);
    memcpy(arp_packet->target_ip, target_ipaddr->array, XNET_IPV4_ADDR_SIZE);

    // 发送以太网请求
    return ethernet_out_to(XNET_PROTOCOL_ARP, ether_broadcast, xnet_packet);
}

/**
 * 以太网初始化，此时会写入协议栈 mac 地址
 * @return 初始化结果
 */
static xnet_err_t ethernet_init(void) {
    xnet_err_t err = xnet_driver_open(netif_mac);
    if (err < 0) return err;
    // 全网广播自己的 mac 地址，target ip设置自己
    return xarp_make_request(&netif_ipaddr);
}

/**
 * 更新ARP表项
 * @param src_ip 源IP地址
 * @param mac_addr 对应的mac地址
 */
static void update_arp_entry(uint8_t *src_ip, uint8_t *mac_addr) {
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
 * ARP输入处理
 * @param packet 输入的ARP包
 */
static void xarp_in(xnet_packet_t *packet) {
    // 如果小于，说明数据错误，直接忽略这个arp请求
    if (packet->size >= sizeof(xarp_packet_t)) {
        xarp_packet_t *arp_packet = (xarp_packet_t *) packet->data;
        uint16_t opcode = swap_order16(arp_packet->opcode);

        // 只处理发给自己的请求或响应包，此处不处理无回报的ARP包
        if (!xipaddr_is_equal_buf(&netif_ipaddr, arp_packet->target_ip)) {
            return;
        }

        // 包的合法性检查
        if ((swap_order16(arp_packet->hw_type) != XARP_HW_ETHER) ||
            (arp_packet->hw_len != XNET_MAC_ADDR_SIZE) ||
            (swap_order16(arp_packet->protocol_type) != XNET_PROTOCOL_IP) ||
            (arp_packet->protocol_len != XNET_IPV4_ADDR_SIZE)
            || ((opcode != XARP_REQUEST) && (opcode != XARP_REPLY))) {
            return;
        }

        // 根据操作码进行不同的处理
        switch (swap_order16(arp_packet->opcode)) {
            case XARP_REQUEST: // 请求，回送响应
                // 在对方机器Ping 自己，然后看wireshark，能看到ARP请求和响应
                // 接下来，很可能对方要与自己通信，所以更新一下
                update_arp_entry(arp_packet->sender_ip, arp_packet->sender_mac);
                xarp_make_response(arp_packet->sender_ip, arp_packet->sender_mac);
                break;
            case XARP_REPLY: // 响应，更新自己的表
                update_arp_entry(arp_packet->sender_ip, arp_packet->sender_mac);
                break;
        }
    }
}

/**
 * 以太网数据帧输入输出
 * @param packet 待处理的包
 */
static void ethernet_in(xnet_packet_t *packet) {
    // 至少要比头部数据大
    if (packet->size <= sizeof(xether_hdr_t)) {
        return;
    }

    // 往上分解到各个协议处理
    xether_hdr_t *hdr = (xether_hdr_t *) packet->data;
    // 协议类型占用两个字节，需要大小端转换
    switch (swap_order16(hdr->protocol)) {
        case XNET_PROTOCOL_ARP:
            remove_header(packet, sizeof(xether_hdr_t));
            xarp_in(packet);
            break;
        case XNET_PROTOCOL_IP: {
            break;
        }
    }
}

/**
 * 查询网络接口，看看是否有数据包，有则进行处理
 */
static void ethernet_poll(void) {
    xnet_packet_t *packet;
    // 此处使用二级指针，给packet赋值
    if (xnet_driver_read(&packet) == XNET_ERR_OK) {
        // 只要轮询到了数据，就会进入这里
        // 正常情况下，在此打个断点，全速运行
        // 然后在对方端ping 192.168.254.2，会停在这里
        ethernet_in(packet);
    }
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
