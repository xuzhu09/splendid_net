//
// Created by efairy520 on 2025/12/9.
//

#include "xnet_tcp.h"

#include <stdlib.h>
#include <string.h>
#include "xnet_ethernet.h"
#include "xnet_ip.h"

// TCP 序列号生成宏
#define tcp_get_init_seq() ((rand() << 16) + rand())

#define XTCP_FLAG_FIN    (1 << 0)
#define XTCP_FLAG_SYN    (1 << 1)
#define XTCP_FLAG_RST    (1 << 2)
#define XTCP_FLAG_ACK    (1 << 4)

#define XTCP_KIND_END       0
#define XTCP_KIND_MSS       2
#define XTCP_MSS_DEFAULT    1460
#define XTCP_WIN_DEFAULT    65535
// TCP最大数据载荷 = MTU(1500) - IPv4固定头(20) - TCP固定头(20) = 1460
#define XTCP_DATA_MAX_SIZE (XNET_CFG_MTU - 20 - 20)

// 1. TCP PCB 最大数量
#define XTCP_PCB_MAX_NUM   40

// 利用补码特性完美解决回绕比较问题
#define TCP_SEQ_LT(a, b)   ((int32_t)((a) - (b)) < 0)
#define TCP_SEQ_LEQ(a, b)  ((int32_t)((a) - (b)) <= 0)

// ===== _hdrlen_rsvd_flags =====
// 提取高 4 位: 头部长度 (Data Offset)
#define TCP_HDR_GET_LEN(_hdrlen_rsvd_flags)     (((_hdrlen_rsvd_flags) >> 12) & 0x000F)

// 提取低 6 位: 标志位 (Flags)
#define TCP_HDR_GET_FLAGS(_hdrlen_rsvd_flags)   ((_hdrlen_rsvd_flags) & 0x003F)

// 组装 16 bit: 长度 + 保留位(默认0) + 标志位
#define TCP_HDR_SET_FLAGS(hdr_len, flags)          ((((hdr_len) & 0x000F) << 12) | ((flags) & 0x003F))

// pcb数组，程序启动自动创建，属性全部为0
static xtcp_pcb_t tcp_pcb_pool[XTCP_PCB_MAX_NUM];

#pragma pack(1)
// TCP头部 20个字节（可能还有12字节的选项数据）
typedef struct _xtcp_hdr_t {
    uint16_t src_port;           // 源端口号 (Source Port)
    uint16_t dest_port;          // 目的端口号 (Destination Port)
    uint32_t seq;                // 序列号 (Sequence Number)：标记本端发送的数据字节流序号
    uint32_t ack;                // 确认号 (Acknowledgment Number)：期望收到对方下一个字节的序号
    uint16_t _hdrlen_rsvd_flags; // 首部长度(高4位) + 保留位(中间6位) + 标志位FIN/SYN/RST等(低6位)
    uint16_t window;             // 接收窗口 (Window Size)：告诉对方我还能接收多少字节，用于流量控制
    uint16_t checksum;           // 校验和 (Checksum)：涵盖 IP 伪头部、TCP 头部及 payload 数据
    uint16_t urgent_ptr;         // 紧急指针 (Urgent Pointer)：当标志位 URG=1 时有效，表示紧急数据的位置
} xtcp_hdr_t;
#pragma pack()

// ===== accept queue helpers (listener-owned, lwIP-like) =====

static void tcp_acceptq_push(xtcp_pcb_t *listen, xtcp_pcb_t *child) {
    child->accept_next = NULL;

    if (listen->accept_head == NULL) {
        listen->accept_head = child;
        listen->accept_tail = child;
    } else {
        listen->accept_tail->accept_next = child;
        listen->accept_tail = child;
    }

    listen->accept_cnt++;
}

static xtcp_pcb_t *tcp_acceptq_pop(xtcp_pcb_t *listen) {
    xtcp_pcb_t *child = listen->accept_head;
    if (!child) return NULL;

    listen->accept_head = child->accept_next;
    if (listen->accept_head == NULL) {
        listen->accept_tail = NULL;
    }

    child->accept_next = NULL;
    listen->accept_cnt--;
    return child;
}


// 计算两个指针之间的距离
static uint16_t tcp_buf_dist(uint16_t from, uint16_t to) {
    // from是数据的开始，to是数据的结束
    return (to >= from) ? (to - from) : (XTCP_CFG_RTX_BUF_SIZE + to - from);
}

static void tcp_buf_init(xtcp_buf_t *tcp_buf) {
    tcp_buf->write_idx = 0;
    tcp_buf->ack_idx = 0;
    tcp_buf->send_idx = 0;
}

// 计算缓冲区当前总占用量 (包含已发未确认 + 待发送)
static uint16_t tcp_buf_used_count(xtcp_buf_t *tcp_buf) {
    // 也就是：从 ack_idx 到 write_idx 的距离
    return tcp_buf_dist(tcp_buf->ack_idx, tcp_buf->write_idx);
}

// 计算剩余空闲空间
static uint16_t tcp_buf_free_count(xtcp_buf_t *tcp_buf) {
    // 总容量 - 已用容量
    // 已用容量 = dist(ack_idx, write_idx)
    // 总容量 = 缓冲区长度 - 1（因为永远都要空一格）
    return (XTCP_CFG_RTX_BUF_SIZE - 1) - tcp_buf_used_count(tcp_buf);
}

// 计算有多少数据等待发送,绿色区域
static uint16_t tcp_buf_wait_send_count(xtcp_buf_t *tcp_buf) {
    // 从 send_idx 到 write_idx 的距离
    return tcp_buf_dist(tcp_buf->send_idx, tcp_buf->write_idx);
}

// 收到 ACK，释放空间
static void tcp_buf_advance_ack(xtcp_buf_t *tcp_buf, uint16_t len) {
    // 本质是：向前移动 ack_idx 指针
    tcp_buf->ack_idx += len;
    // 处理回绕
    if (tcp_buf->ack_idx >= XTCP_CFG_RTX_BUF_SIZE) {
        tcp_buf->ack_idx -= XTCP_CFG_RTX_BUF_SIZE;
    }
}

// 数据已发送给网卡，标记为"已发未确认"
static void tcp_buf_advance_send(xtcp_buf_t *tcp_buf, uint16_t len) {
    // 本质是：向前移动 send_idx 指针
    tcp_buf->send_idx += len;
    // 处理回绕
    if (tcp_buf->send_idx >= XTCP_CFG_RTX_BUF_SIZE) {
        tcp_buf->send_idx -= XTCP_CFG_RTX_BUF_SIZE;
    }
}

// 从 TCP 发送缓冲区中读取数据拷贝到 packet 中
// 注意：此函数不修改 tcp_buf 的任何指针，仅仅是 Copy
static void tcp_buf_peek(xtcp_buf_t *tcp_buf, uint8_t *dest, uint16_t len) {
    // 使用局部变量 cursor，不触碰 tcp_buf->send_idx
    uint16_t cursor = tcp_buf->send_idx;

    for (uint16_t i = 0; i < len; i++) {
        // 1. 搬运数据
        dest[i] = tcp_buf->data[cursor];

        // 2. 移动局部光标
        cursor++;

        // 3. 处理局部光标的回绕
        if (cursor >= XTCP_CFG_RTX_BUF_SIZE) {
            cursor = 0;
        }
    }
}

// 将数据拼接到缓冲区
static uint16_t tcp_buf_put(xtcp_buf_t *tcp_buf, uint8_t *src, uint16_t len) {
    // 待写入数据与剩余空间，取最小值
    uint16_t copy_len = min(len, tcp_buf_free_count(tcp_buf));

    // 一对一复制，移动write_idx指针（下个可写入位置）
    for (uint16_t i = 0; i < copy_len; i++) {
        tcp_buf->data[tcp_buf->write_idx++] = *src++;
        if (tcp_buf->write_idx >= XTCP_CFG_RTX_BUF_SIZE) {
            tcp_buf->write_idx = 0; //遇到边缘回绕
        }
    }

    return copy_len;
}

static uint16_t tcp_recv(xtcp_pcb_t *pcb, uint8_t flags, uint8_t *src, uint16_t len) {
    // 1. 将收到的 payload 写入接收缓冲区 (rx_buf)
    uint16_t read_len = tcp_buf_put(&pcb->rx_buf, src, len);

    // 2. 累加 ACK 号，准备下次告诉对方我收到了多少
    pcb->rcv_nxt += read_len;

    // 3. 如果收到 FIN 或 SYN，也要消耗一个序列号
    if (flags & (XTCP_FLAG_FIN | XTCP_FLAG_SYN)) {
        pcb->rcv_nxt++;
    }
    return read_len;
}

// 从 TCP 接收缓冲区读取数据 (消费数据)
// 参数 dest: 目标缓冲区 (Destination)
// 参数 len:  想读取的长度 (Length)
static uint16_t tcp_buf_pull(xtcp_buf_t *tcp_buf, uint8_t *dest, uint16_t len) {

    // 1. 获取库存量
    uint16_t used = tcp_buf_used_count(tcp_buf);

    // 2. 裁切长度：实际能读的长度 (Actual Read Length)
    uint16_t read_len = (len > used) ? used : len;

    // 3. 循环搬运
    //    类型修正：用 uint16_t 替代 int
    for (uint16_t i = 0; i < read_len; i++) {

        // Step A: 搬运数据 (Buffer -> Destination)
        // 清晰：把缓冲区当前 ack 指向的数据，给到目标
        *dest = tcp_buf->data[tcp_buf->ack_idx];

        // Step B: 移动目标指针
        dest++;

        // Step C: 移动缓冲区消费指针 (ack_idx)
        tcp_buf->ack_idx++;

        // Step D: 处理回绕
        if (tcp_buf->ack_idx >= XTCP_CFG_RTX_BUF_SIZE) {
            tcp_buf->ack_idx = 0;
        }
    }

    return read_len;
}

static xnet_status_t tcp_send_reset(uint32_t ack_seq, uint16_t local_port, xip_addr_t *remote_ip, uint16_t remote_port) {
    // TCP发送复位，只有头部，没有选项
    xnet_packet_t *packet = xnet_prepare_tx_packet(sizeof(xtcp_hdr_t));
    xtcp_hdr_t *tcp_hdr = (xtcp_hdr_t*) packet->data;

    tcp_hdr->src_port = swap_order16(local_port);
    tcp_hdr->dest_port = swap_order16(remote_port);
    tcp_hdr->seq = 0;
    tcp_hdr->ack = swap_order32(ack_seq);
    uint16_t hdr_len = sizeof(xtcp_hdr_t) / 4;
    uint16_t flags = XTCP_FLAG_RST | XTCP_FLAG_ACK;
    tcp_hdr->_hdrlen_rsvd_flags = swap_order16(TCP_HDR_SET_FLAGS(hdr_len, flags));
    tcp_hdr->window = 0;
    tcp_hdr->checksum = 0;
    tcp_hdr->urgent_ptr = 0;

    tcp_hdr->checksum = pseudo_checksum(&xnet_local_ip, remote_ip, XNET_PROTOCOL_TCP, (uint16_t*)packet->data, packet->len);
    tcp_hdr->checksum = tcp_hdr->checksum ? tcp_hdr->checksum : 0xFFFF;
    return xip_out(XNET_PROTOCOL_TCP, remote_ip, packet);
}

static void tcp_read_mss(xtcp_pcb_t *pcb, xtcp_hdr_t *tcp_hdr) {
    uint16_t actual_hdr_len = TCP_HDR_GET_LEN(tcp_hdr->_hdrlen_rsvd_flags) * 4;
    // 真实长度 - 理论长度 = 选项长度
    uint16_t opt_len = actual_hdr_len - sizeof(xtcp_hdr_t);

    if (opt_len == 0) {
        pcb->remote_mss = XTCP_MSS_DEFAULT;
    } else {
        uint8_t *opt_data = (uint8_t*)tcp_hdr + sizeof(xtcp_hdr_t);
        uint8_t *opt_end = opt_data + opt_len;

        while ((*opt_data != XTCP_KIND_END) && (opt_data < opt_end)) {
            if ((*opt_data++ == XTCP_KIND_MSS) && (*opt_data++ == 4)) {
                pcb->remote_mss = swap_order16(*(uint16_t *)opt_data);
                return;
            }
        }
    }
}

// 通用发送数据方法
static xnet_status_t tcp_send_segment(xtcp_pcb_t *pcb, uint8_t flags) {
    uint16_t payload_len = tcp_buf_wait_send_count(&pcb->tx_buf);
    // SYN 包需要额外4字节的MSS选项空间
    uint16_t opt_len = (flags & XTCP_FLAG_SYN) ? 4 : 0;

    if (pcb->remote_win) {
        payload_len = min(payload_len, pcb->remote_win);    //data_size不能超过对方窗口
        payload_len = min(payload_len, pcb->remote_mss);    //data_size不能超过对方单包最大限制
        if (payload_len + opt_len > XTCP_DATA_MAX_SIZE) {//data_size不能超过以太网剩余限制,1460
            payload_len = XTCP_DATA_MAX_SIZE - opt_len;
        }
    } else {
        payload_len = 0;
    }

    xnet_packet_t *packet = xnet_prepare_tx_packet(payload_len + opt_len + sizeof(xtcp_hdr_t));
    xtcp_hdr_t *tcp_hdr = (xtcp_hdr_t*) packet->data;

    tcp_hdr->src_port = swap_order16(pcb->local_port);
    tcp_hdr->dest_port = swap_order16(pcb->remote_port);
    tcp_hdr->seq = swap_order32(pcb->snd_nxt); //由上一次发送的seq确定
    tcp_hdr->ack = swap_order32(pcb->rcv_nxt); //由上一次收到的seq确定
    uint16_t len_val = (opt_len + sizeof(xtcp_hdr_t)) / 4;
    tcp_hdr->_hdrlen_rsvd_flags = swap_order16(TCP_HDR_SET_FLAGS(len_val, flags));
    tcp_hdr->window = swap_order16(tcp_buf_free_count(&pcb->rx_buf));
    tcp_hdr->checksum = 0;
    tcp_hdr->urgent_ptr = 0;
    if (flags & XTCP_FLAG_SYN) {
        uint8_t *opt_data = packet->data + sizeof(xtcp_hdr_t);
        opt_data[0] = XTCP_KIND_MSS;
        opt_data[1] = 4;
        *(uint16_t*)(opt_data + 2) = swap_order16(XTCP_MSS_DEFAULT);
    }
    // 将pcb发送缓冲区的数据拷贝到packet
    tcp_buf_peek(&pcb->tx_buf, packet->data + opt_len + sizeof(xtcp_hdr_t), payload_len);

    tcp_hdr->checksum = pseudo_checksum(&xnet_local_ip, &pcb->remote_ip, XNET_PROTOCOL_TCP,
                                     (uint16_t*)packet->data, packet->len);
    tcp_hdr->checksum = tcp_hdr->checksum ? tcp_hdr->checksum : 0xFFFF;

    xnet_status_t status = xip_out(XNET_PROTOCOL_TCP, &pcb->remote_ip, packet);
    if (status < 0) return status;

    // 发送后，移动send_idx
    if (payload_len > 0) {
        tcp_buf_advance_send(&pcb->tx_buf, payload_len);
    }

    pcb->remote_win -= payload_len;
    pcb->snd_nxt += payload_len;


    if (flags & (XTCP_FLAG_SYN | XTCP_FLAG_FIN)) {
        pcb->snd_nxt++; // SYN或FIN占用1个字节，ACK不占用
    }
    return XNET_OK;
}

static void tcp_pcb_free(xtcp_pcb_t *pcb) {
    if (!pcb) return;
    pcb->state = XTCP_STATE_FREE;
}

// 监听状态下的输入处理（发送第二次握手）
static void tcp_listen_input(xtcp_pcb_t *listen_pcb, xip_addr_t *remote_ip, xtcp_hdr_t *tcp_hdr) {
    // 安全提取标志位
    uint16_t flags = TCP_HDR_GET_FLAGS(tcp_hdr->_hdrlen_rsvd_flags);

    // 非 SYN 包直接 RST
    if (!(flags & XTCP_FLAG_SYN)) {
        tcp_send_reset(tcp_hdr->seq, listen_pcb->local_port, remote_ip, tcp_hdr->src_port);
        return;
    }

    // 1. 拿一个标准件 (此时缓冲区、随机Seq都准备好了！)
    xtcp_pcb_t *child_pcb = xtcp_pcb_new();
    if (!child_pcb) return;

    // 2. 个性化配置 (连接侧特有)
    // 2.1 继承“父业”
    child_pcb->local_port = listen_pcb->local_port; // 继承端口
    child_pcb->listener = listen_pcb;   // ✅ 记录父 LISTEN pcb（S方案关键）

    // 2.2 录入“客人”信息
    child_pcb->state = XTCP_STATE_SYN_RECVD;      // 状态跃迁
    child_pcb->remote_ip = *remote_ip;
    child_pcb->remote_port = tcp_hdr->src_port;
    child_pcb->remote_win = tcp_hdr->window;

    // 2.3 同步进度 (Seq)
    child_pcb->rcv_nxt = tcp_hdr->seq + 1;

    // 3. 协议协商 (MSS)
    tcp_read_mss(child_pcb, tcp_hdr);

    // 4. 发送 SYN+ACK
    xnet_status_t status = tcp_send_segment(child_pcb, XTCP_FLAG_SYN | XTCP_FLAG_ACK);
    if (status < 0) {
        tcp_pcb_free(child_pcb);
    }
}

void xtcp_init(void) {
    // 整体清零，确保所有状态为 XTCP_STATE_FREE (0)
    memset(tcp_pcb_pool, 0, sizeof(tcp_pcb_pool));
}

void xtcp_in(xip_addr_t *remote_ip, xnet_packet_t *packet) {
    // 检查TCP包的长度
    if (packet->len < sizeof(xtcp_hdr_t)) {
        return;
    }
    // 检查伪校验和（校验和不需要进行大小端转换，校验和算法与顺序无关）
    xtcp_hdr_t *tcp_hdr = (xtcp_hdr_t*) packet->data;
    uint16_t pre_checksum = tcp_hdr->checksum;
    tcp_hdr->checksum = 0;
    if (pre_checksum != 0) {
        uint16_t checksum = pseudo_checksum(remote_ip, &xnet_local_ip, XNET_PROTOCOL_TCP, (uint16_t*) tcp_hdr, packet->len);
        checksum = (checksum == 0) ? 0xFFFF : checksum;
        if (checksum != pre_checksum) {
            return;
        }
    }

    // 大小端转换
    tcp_hdr->src_port = swap_order16(tcp_hdr->src_port);
    tcp_hdr->dest_port = swap_order16(tcp_hdr->dest_port);
    tcp_hdr->_hdrlen_rsvd_flags = swap_order16(tcp_hdr->_hdrlen_rsvd_flags);
    tcp_hdr->seq = swap_order32(tcp_hdr->seq);
    tcp_hdr->ack = swap_order32(tcp_hdr->ack);
    tcp_hdr->window = swap_order16(tcp_hdr->window);

    // 查询五元组
    xtcp_pcb_t *pcb = xtcp_pcb_find(remote_ip, tcp_hdr->src_port, tcp_hdr->dest_port);
    if (pcb == NULL) {
        // 找不到pcb，说明连listen pcb都没有，没有应用程序监听目标端口
        tcp_send_reset(tcp_hdr->seq + 1, tcp_hdr->dest_port, remote_ip, tcp_hdr->src_port);
        return;
    }

    // pcb处于监听状态（收到第一次握手）
    if (pcb->state == XTCP_STATE_LISTEN) {
        // 监听状态下的输入处理（发送第二次握手）
        tcp_listen_input(pcb, remote_ip, tcp_hdr);
        return;
    }

    // 校验序列号
    if (tcp_hdr->seq != pcb->rcv_nxt) {
        // 场景：对方发了 1, 2。我先收到了 2。
        // 正确做法：丢弃包 2（因为我处理不了乱序），
        // 并且发一个 ACK 告诉对方："我还在等 1 呢 (rcv_nxt)"。
        // 对方收到这个重复 ACK 后，就会意识到丢包了，会触发快速重传。
        // 只有当连接已经建立后，才发 ACK 纠正对方；否则发 RST
        if (pcb->state == XTCP_STATE_ESTABLISHED) {
            tcp_send_segment(pcb, XTCP_FLAG_ACK);
        } else {
            tcp_send_reset(tcp_hdr->seq + 1, tcp_hdr->dest_port, remote_ip, tcp_hdr->src_port);
        }
        return;
    }

    // 这里可能是第三次握手，也可能是连接已建立后的正常通信，此时tcp_hdr可能包含option数据，所以不能使用sizeof(xtcp_hdr_t)
    uint16_t actual_hdr_len = TCP_HDR_GET_LEN(tcp_hdr->_hdrlen_rsvd_flags) * 4;
    // 头部声称的长度不能小于基础 20 字节，也不能大于当前整个包的物理长度
    if (actual_hdr_len < sizeof(xtcp_hdr_t) || actual_hdr_len > packet->len) {
        return; // 非法畸形包，直接丢弃
    }
    remove_header(packet, actual_hdr_len);

    // 使用安全宏提取标志位
    uint16_t flags = TCP_HDR_GET_FLAGS(tcp_hdr->_hdrlen_rsvd_flags);
    uint16_t payload_len = packet->len; // 剥离头后，这就是数据长度

    switch (pcb->state) {
        case XTCP_STATE_SYN_RECVD:
            if (flags & XTCP_FLAG_ACK) {
                // 第三次握手 ACK 必须确认到我发出的 SYN+ACK（snd_nxt 已在 tcp_send_segment 里 +1）
                if (tcp_hdr->ack == pcb->snd_nxt) {

                    pcb->snd_una++;  // 确认 SYN 已被对方确认
                    pcb->state = XTCP_STATE_ESTABLISHED;

                    // ✅ 入队到父 listener 的 accept 队列（而不是全局队列）
                    xtcp_pcb_t *listen = pcb->listener;
                    if (listen && listen->state == XTCP_STATE_LISTEN) {
                        if (listen->accept_cnt < listen->backlog) {
                            tcp_acceptq_push(listen, pcb);
                        } else {
                            // backlog 满：直接拒绝（RST 或 close 都行，这里用 RST 更干脆）
                            tcp_send_reset(pcb->rcv_nxt, pcb->local_port, &pcb->remote_ip, pcb->remote_port);
                            tcp_pcb_free(pcb);
                        }
                    } else {
                        // 没有 listener（异常）就回收
                        tcp_pcb_free(pcb);
                    }
                }
            }
            break;
        case XTCP_STATE_ESTABLISHED: //连接已建立，大部分情况都会走这里
            if ((flags & XTCP_FLAG_ACK)) {
                // 实时更新对方的接收窗口。
                // 即使 ACK 号没有变（即没有确认新数据），对方也可能只是发个包来告诉我们窗口变大了。
                pcb->remote_win = tcp_hdr->window;
                if (TCP_SEQ_LT(pcb->snd_una, tcp_hdr->ack) && TCP_SEQ_LEQ(tcp_hdr->ack, pcb->snd_nxt)) {
                    // 计算对方确认了多少字节
                    uint32_t acked_len = tcp_hdr->ack - pcb->snd_una;

                    // 释放发送缓冲区空间 (这一步把 tail 指针前移了)
                    tcp_buf_advance_ack(&pcb->tx_buf, acked_len);
                    pcb->snd_una += acked_len;
                }
            }

            // 定义一个标志位：是否需要回复 ACK
            int need_ack = 0;

            // 显式处理接收到的数据
            if (payload_len > 0) {
                // 1. 直接调用 buffer 的 put 方法 (语义清晰：放入接收缓冲)
                int written = tcp_buf_put(&pcb->rx_buf, packet->data, payload_len);

                // 2. 更新接收进度 (只加数据的长度)
                pcb->rcv_nxt += written;

                // 收到了新数据，必须回复 ACK
                need_ack = 1;
            }

            // 作为服务端，收到第一次挥手FIN
            if ((flags & XTCP_FLAG_FIN)) {
                pcb->state = XTCP_STATE_CLOSE_WAIT; // 注意：服务端收到FIN通常进入CLOSE_WAIT
                pcb->rcv_nxt++; // FIN 占用一个序列号
                // 收到 FIN，必须回复 ACK
                need_ack = 1;
            }
            // 4. 统一发送 ACK / 数据
            if (need_ack) {
                // 必须回 ACK (可能带有 FIN 的 ACK，或者数据的 ACK)
                // 如果刚好自己也有数据要发，tcp_send_segment 会自动带上 payload (捎带应答)
                tcp_send_segment(pcb, XTCP_FLAG_ACK);
            }else if (tcp_buf_wait_send_count(&pcb->tx_buf)) {
                // 虽然没收到新数据不需要立即 ACK，但我自己有数据要发
                // 这种情况下也要发包 (ACK 标志也是必须带的)
                tcp_send_segment(pcb, XTCP_FLAG_ACK);
            }
            break;
        case XTCP_STATE_FIN_WAIT_1:
            // 作为客户端，直接收到第三次挥手
            if ((flags & XTCP_FLAG_FIN) && (flags & XTCP_FLAG_ACK)) {
                tcp_pcb_free(pcb);
            }
            // 作为客户端，收到第二次挥手
            else if ((flags & XTCP_FLAG_ACK)) {
                pcb->state = XTCP_STATE_FIN_WAIT_2;
            }
            break;
        case XTCP_STATE_FIN_WAIT_2:
            // 检查是否收到了对方的 FIN 包（即第三次挥手的 FIN 部分）
            if ((flags & XTCP_FLAG_FIN)) {
                pcb->rcv_nxt++;
                tcp_send_segment(pcb,XTCP_FLAG_ACK);
                tcp_pcb_free(pcb);
            }
            break;
        case XTCP_STATE_LAST_ACK:
            if (flags & XTCP_FLAG_ACK) {
                tcp_pcb_free(pcb);
            }
            break;
    }
}

// 新建 TCP PCB
xtcp_pcb_t *xtcp_pcb_new(void) {
    for (xtcp_pcb_t *pcb = tcp_pcb_pool; pcb < &tcp_pcb_pool[XTCP_PCB_MAX_NUM]; pcb++) {
        // 找到空闲槽位
        if (pcb->state == XTCP_STATE_FREE) {

            // A. 物理清零 (这一步连同里面的 tx_buf 和 rx_buf 实体一起全清零了！)
            memset(pcb, 0, sizeof(xtcp_pcb_t));

            // B. 状态初始化
            pcb->state = XTCP_STATE_CLOSED;

            // C. 核心组件初始化 (这是之前 accept 里经常漏写的！)
            tcp_buf_init(&pcb->tx_buf);
            tcp_buf_init(&pcb->rx_buf);

            // D. 协议参数初始化 (出厂默认值)
            pcb->remote_mss = XTCP_MSS_DEFAULT;
            pcb->remote_win = XTCP_WIN_DEFAULT;

            // E. 身份标识初始化 (随机序列号)
            // 无论是主动连别人，还是被动接受，我都得有个随机的初始 Seq
            pcb->snd_nxt = tcp_get_init_seq();
            pcb->snd_una = pcb->snd_nxt;

            // 返回这个“标准件”
            return pcb;
        }
    }
    return NULL;
}

xnet_status_t xtcp_pcb_bind(xtcp_pcb_t *pcb, uint16_t local_port) {
    if (pcb == NULL || local_port == 0) {
        return XNET_ERR_PARAM;
    }

    // 1. 检查端口是否已占用
    for (xtcp_pcb_t *curr = tcp_pcb_pool; curr < &tcp_pcb_pool[XTCP_PCB_MAX_NUM]; curr++) {
        // 如果 curr 是 FREE 的，即使它残留的 local_port 也是这个值，也不算冲突。
        if (curr != pcb && curr->state != XTCP_STATE_FREE && curr->local_port == local_port) {
            return XNET_ERR_BINDED;
        }
    }

    // 2. 绑定
    pcb->local_port = local_port;
    return XNET_OK;
}

// 接收到请求后，找到匹配的 pcb
// 优先级：已建立连接的五元组匹配 > 监听端口匹配
xtcp_pcb_t *xtcp_pcb_find(xip_addr_t *remote_ip, uint16_t remote_port, uint16_t local_port) {
    xtcp_pcb_t *listen_pcb = NULL;

    for (xtcp_pcb_t *curr = tcp_pcb_pool; curr < &tcp_pcb_pool[XTCP_PCB_MAX_NUM]; curr++) {
        // 0. FREE 的直接跳过
        if (curr->state == XTCP_STATE_FREE) {
            continue;
        }

        // 1. 本地端口必须匹配
        if (curr->local_port != local_port) {
            continue;
        }

        // 2. 检查五元组精确匹配（针对 ESTABLISHED, SYN_SENT 等活动连接）
        // 只有远程 IP 和端口都匹配，才是真正的“当前连接”
        // 【逻辑调整】：建议先判断精确匹配，这样逻辑流更顺畅，虽然结果一样。
        if (xip_addr_eq(remote_ip, &curr->remote_ip) && (remote_port == curr->remote_port)) {
            return curr; // 找到唯一活动连接，直接返回
        }

        // 3. 检查 LISTEN 状态 (作为备选)
        if (curr->state == XTCP_STATE_LISTEN) {
            listen_pcb = curr;
        }
    }

    // 4. 如果没找到活动连接，返回监听 listen pcb（如果有的话）用来建立新连接
    return listen_pcb;
}

xnet_status_t xtcp_pcb_listen(xtcp_pcb_t *pcb, uint8_t backlog) {
    if (pcb == NULL) return XNET_ERR_PARAM;

    if (pcb->state != XTCP_STATE_CLOSED) {
        return XNET_ERR_STATE;
    }
    if (pcb->local_port == 0) {
        return XNET_ERR_PARAM;
    }

    pcb->state = XTCP_STATE_LISTEN;

    // ===== accept queue init =====
    pcb->accept_head = NULL;
    pcb->accept_tail = NULL;
    pcb->accept_cnt  = 0;
    pcb->backlog     = backlog;

    return XNET_OK;
}


// 使用tcp发送数据
int xtcp_send(xtcp_pcb_t *pcb, uint8_t *src, uint16_t len) {
    if ((pcb->state != XTCP_STATE_ESTABLISHED)) {
        return -1;
    }
    // 将数据拷贝到&pcb->tx_buf，移动write_idx
    uint16_t written = tcp_buf_put(&pcb->tx_buf, src, len);
    if (written > 0) {
        tcp_send_segment(pcb, XTCP_FLAG_ACK); // 只要连接建立，每一次发送都要带ACK
    }
    return written;
}

// 使用tcp接收数据
int xtcp_recv(xtcp_pcb_t *pcb, uint8_t *dest, uint16_t len) {
    int read_len = tcp_buf_pull(&pcb->rx_buf, dest, len);

    // 【新增逻辑】窗口更新机制
    if (read_len > 0) {
        // 如果当前缓冲区比较空（比如空闲空间 > MSS），就告诉对方
        // 这里简单处理：只要读走数据，就尝试发个 ACK 更新窗口
        if (pcb->state == XTCP_STATE_ESTABLISHED) {
            // 这里的 flags 仅传 ACK，tcp_send_segment 会自动把最新的 rx_buf 剩余空间填入 header->window
            tcp_send_segment(pcb, XTCP_FLAG_ACK);
        }
    }

    return read_len;
}

// 服务端主动关闭连接
xnet_status_t xtcp_pcb_close(xtcp_pcb_t *pcb) {
    xnet_status_t status;

    if (pcb->state == XTCP_STATE_ESTABLISHED) {
        status = tcp_send_segment(pcb, XTCP_FLAG_FIN | XTCP_FLAG_ACK); // 只要连接已建立，必然带ACK
        if (status < 0) return status;
        pcb->state = XTCP_STATE_FIN_WAIT_1;
    } else if (pcb->state == XTCP_STATE_CLOSE_WAIT) {
        // 增加被动关闭的分支
        tcp_send_segment(pcb, XTCP_FLAG_FIN | XTCP_FLAG_ACK);
        pcb->state = XTCP_STATE_LAST_ACK; // 等待对方最后的 ACK
    } else {
        tcp_pcb_free(pcb);
    }
    return XNET_OK;
}

xtcp_pcb_t *xtcp_accept(xtcp_pcb_t *listen_pcb) {
    if (!listen_pcb || listen_pcb->state != XTCP_STATE_LISTEN) {
        return NULL;
    }
    return tcp_acceptq_pop(listen_pcb);
}