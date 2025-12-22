//
// Created by efairy520 on 2025/12/9.
//

#include "xnet_tcp.h"

#include <stdlib.h>
#include <string.h>
#include "xnet_ethernet.h"
#include "xnet_ip.h"

// pcb数组，程序启动自动创建，属性全部为0
static xtcp_pcb_t tcp_pcb_pool[XTCP_PCB_MAX_NUM];

static void tcp_buf_init(xtcp_buf_t* tcp_buf) {
    tcp_buf->front = tcp_buf->tail = 0;
    tcp_buf->next = 0;
    tcp_buf->data_count = tcp_buf->unacked_count = 0;
}

static uint16_t tcp_buf_free_count(xtcp_buf_t* tcp_buf) {
    return XTCP_CFG_RTX_BUF_SIZE - tcp_buf->data_count;
}

static uint16_t tcp_buf_wait_send_count(xtcp_buf_t* tcp_buf) {
    return tcp_buf->data_count - tcp_buf->unacked_count;
}

static void tcp_buf_add_acked_count(xtcp_buf_t* tcp_buf, uint16_t size) {
    tcp_buf->tail += size;
    if (tcp_buf->tail >= XTCP_CFG_RTX_BUF_SIZE) {
        tcp_buf->tail -= XTCP_CFG_RTX_BUF_SIZE;
    }

    tcp_buf->data_count -= size;
    tcp_buf->unacked_count -= size;
}

static uint16_t tcp_buf_add_unacked_count(xtcp_buf_t* tcp_buf, uint16_t size) {
    tcp_buf->unacked_count += size;
    return tcp_buf->unacked_count;
}

static uint16_t tcp_buf_read_for_send(xtcp_buf_t* tcp_buf, uint8_t* to, uint16_t size) {
    int i;

    uint16_t wait_send_count = tcp_buf->data_count - tcp_buf->unacked_count;
    size = min(size, wait_send_count);
    // 移动to指针
    for (i = 0; i < size; i++) {
        *to++ = tcp_buf->data[tcp_buf->next++];
        if (tcp_buf->next >= XTCP_CFG_RTX_BUF_SIZE) {
            tcp_buf->next = 0;
        }
    }

    return size;
}

// 将输入写入到pcb的环形缓冲区
static uint16_t tcp_buf_write(xtcp_buf_t* tcp_buf, uint8_t* from, uint16_t size) {
    int i;

    size = min(size, tcp_buf_free_count(tcp_buf));

    for (i = 0; i < size; i++) {
        tcp_buf->data[tcp_buf->front++] = *from++;
        if (tcp_buf->front >= XTCP_CFG_RTX_BUF_SIZE) {
            tcp_buf->front = 0;
        }
    }

    tcp_buf->data_count += size;
    return size;
}

static uint16_t tcp_recv(xtcp_pcb_t* pcb, uint8_t flags, uint8_t* from, uint16_t size) {
    // 1. 将收到的 payload 写入接收缓冲区 (rx_buf)
    uint16_t read_size = tcp_buf_write(&pcb->rx_buf, from, size);

    // 2. 累加 ACK 号，准备下次告诉对方我收到了多少
    pcb->ack += read_size;

    // 3. 如果收到 FIN 或 SYN，也要消耗一个序列号
    if (flags & (XTCP_FLAG_FIN | XTCP_FLAG_SYN)) {
        pcb->ack++;
    }
    return read_size;
}

static uint16_t tcp_buf_read(xtcp_buf_t* tcp_buf, uint8_t* to, uint16_t size) {
    int i;

    size = min(size, tcp_buf->data_count);
    for (i = 0; i < size; i++) {
        *to++ = tcp_buf->data[tcp_buf->tail++];
        if (tcp_buf->tail >= XTCP_CFG_RTX_BUF_SIZE) {
            tcp_buf->tail = 0;
        }
    }

    tcp_buf->data_count -= size;
    return size;
}

static xnet_status_t tcp_send_reset(uint32_t remote_ack, uint16_t local_port, xip_addr_t* remote_ip, uint16_t remote_port) {
    xnet_packet_t* packet = xnet_alloc_tx_packet(sizeof(xtcp_hdr_t));
    xtcp_hdr_t* tcp_hdr = (xtcp_hdr_t*) packet->data;

    tcp_hdr->src_port = swap_order16(local_port);
    tcp_hdr->dest_port = swap_order16(remote_port);
    tcp_hdr->seq = 0;
    tcp_hdr->ack = swap_order32(remote_ack);
    tcp_hdr->hdr_flags.all = 0;
    tcp_hdr->hdr_flags.hdr_len = sizeof(xtcp_hdr_t) / 4;
    tcp_hdr->hdr_flags.flags = XTCP_FLAG_RST | XTCP_FLAG_ACK;
    tcp_hdr->hdr_flags.all = swap_order16(tcp_hdr->hdr_flags.all);
    tcp_hdr->window = 0;
    tcp_hdr->checksum = 0;
    tcp_hdr->urgent_ptr = 0;

    tcp_hdr->checksum = checksum_peso(&xnet_local_ip, remote_ip, XNET_PROTOCOL_TCP, (uint16_t*)packet->data, packet->length);
    tcp_hdr->checksum = tcp_hdr->checksum ? tcp_hdr->checksum : 0xFFFF;
    return xip_out(XNET_PROTOCOL_TCP, remote_ip, packet);
}

static void tcp_read_mss(xtcp_pcb_t* pcb, xtcp_hdr_t* tcp_hdr) {
    // 真实长度 - 理论长度 = 选项长度
    uint16_t opt_len = tcp_hdr->hdr_flags.hdr_len * 4 - sizeof(xtcp_hdr_t);

    if (opt_len == 0) {
        pcb->remote_mss = XTCP_MSS_DEFAULT;
    } else {
        uint8_t* opt_data = (uint8_t*)tcp_hdr + sizeof(xtcp_hdr_t);
        uint8_t* opt_end = opt_data + opt_len;

        while ((*opt_data != XTCP_KIND_END) && (opt_data < opt_end)) {
            if ((*opt_data++ == XTCP_KIND_MSS) && (*opt_data++ == 4)) {
                pcb->remote_mss = swap_order16(*(uint16_t *)opt_data);
                return;
            }
        }
    }
}
// 通用发送数据方法
static xnet_status_t tcp_send_segment(xtcp_pcb_t* pcb, uint8_t flags) {
    uint16_t data_size = tcp_buf_wait_send_count(&pcb->tx_buf);
    // SYN 包需要额外4字节的MSS选项空间
    uint16_t opt_size = (flags & XTCP_FLAG_SYN) ? 4 : 0;

    if (pcb->remote_win) {
        data_size = min(data_size, pcb->remote_win);    //data_size不能超过对方窗口
        data_size = min(data_size, pcb->remote_mss);    //data_size不能超过对方单包最大限制
        if (data_size + opt_size > XTCP_DATA_MAX_SIZE) {//data_size不能超过以太网剩余限制
            data_size = XTCP_DATA_MAX_SIZE - opt_size;
        }
    } else {
        data_size = 0;
    }

    xnet_packet_t* packet = xnet_alloc_tx_packet(data_size + opt_size + sizeof(xtcp_hdr_t));
    xtcp_hdr_t* tcp_hdr = (xtcp_hdr_t*) packet->data;

    tcp_hdr->src_port = swap_order16(pcb->local_port);
    tcp_hdr->dest_port = swap_order16(pcb->remote_port);
    tcp_hdr->seq = swap_order32(pcb->next_seq); //next_seq由上一次发送的时候确定
    tcp_hdr->ack = swap_order32(pcb->ack); // 由上一次收到的seq确定
    tcp_hdr->hdr_flags.all = 0;
    tcp_hdr->hdr_flags.hdr_len = (opt_size + sizeof(xtcp_hdr_t)) / 4;
    tcp_hdr->hdr_flags.flags = flags;
    tcp_hdr->hdr_flags.all = swap_order16(tcp_hdr->hdr_flags.all);
    tcp_hdr->window = swap_order16(tcp_buf_free_count(&pcb->rx_buf)); // 告诉对方，我现在还能收 ? 字节的数据
    tcp_hdr->checksum = 0;
    tcp_hdr->urgent_ptr = 0;
    if (flags & XTCP_FLAG_SYN) {
        uint8_t* opt_data = packet->data + sizeof(xtcp_hdr_t);
        opt_data[0] = XTCP_KIND_MSS;
        opt_data[1] = 4;
        *(uint16_t*)(opt_data + 2) = swap_order16(XTCP_MSS_DEFAULT);
    }
    // 将pcb环形缓冲区的数据拷贝到packet
    tcp_buf_read_for_send(&pcb->tx_buf, packet->data + opt_size + sizeof(xtcp_hdr_t), data_size);

    tcp_hdr->checksum = checksum_peso(&xnet_local_ip, &pcb->remote_ip, XNET_PROTOCOL_TCP,
                                     (uint16_t*)packet->data, packet->length);
    tcp_hdr->checksum = tcp_hdr->checksum ? tcp_hdr->checksum : 0xFFFF;

    xnet_status_t status = xip_out(XNET_PROTOCOL_TCP, &pcb->remote_ip, packet);
    if (status < 0) return status;

    // 告诉 buffer，这部分数据已经发出去了（虽然还没收到ACK，但已经不在“待发送”队列了）
    if (data_size > 0) {
        tcp_buf_add_unacked_count(&pcb->tx_buf, data_size);
    }

    pcb->remote_win -= data_size;
    pcb->next_seq += data_size;


    if (flags & (XTCP_FLAG_SYN | XTCP_FLAG_FIN)) {
        pcb->next_seq++; // SYN或FIN占用1个字节，ACK不占用
    }
    return XNET_OK;
}

// 分配一个可用pcb，使用zero alloc，避免复用污染
static xtcp_pcb_t* xtcp_pcb_zalloc() {
    for (xtcp_pcb_t* pcb = tcp_pcb_pool; pcb < &tcp_pcb_pool[XTCP_PCB_MAX_NUM]; pcb++) {
        // 找到一个空闲的 pcb
        if (pcb->state == XTCP_STATE_FREE) {
            // 清空旧数据！
            memset(pcb, 0, sizeof(xtcp_pcb_t));

            // 这里先置为 CLOSED，代表内存已分配但未连接
            pcb->state = XTCP_STATE_CLOSED;
            return pcb;
        }
    }
    return NULL;
}

static void xtcp_pcb_init(xtcp_pcb_t* pcb) {
    // 基础配置
    pcb->remote_win = XTCP_WIN_DEFAULT;
    pcb->remote_mss = XTCP_MSS_DEFAULT;

    // 序列号随机化
    pcb->next_seq = tcp_get_init_seq(); // 当前流水号
    pcb->unacked_seq = pcb->next_seq; // 初始时，未确认的就是当前的

    // 缓冲区初始化
    tcp_buf_init(&pcb->tx_buf);
    tcp_buf_init(&pcb->rx_buf);
}

static void tcp_pcb_free(xtcp_pcb_t* pcb) {
    pcb->state = XTCP_STATE_FREE;
}

// 构造child pcb，接受第一次握手数据
static void tcp_process_accept(xtcp_pcb_t* listen_pcb, xip_addr_t* remote_ip, xtcp_hdr_t* tcp_hdr) {
    uint16_t hdr_flags = tcp_hdr->hdr_flags.all;

    // 只有 SYN 才能触发 Accept 逻辑
    if (!(hdr_flags & XTCP_FLAG_SYN)) {
        tcp_send_reset(tcp_hdr->seq, listen_pcb->local_port, remote_ip, tcp_hdr->src_port);
        return;
    }

    xtcp_pcb_t* child_pcb = xtcp_pcb_zalloc();
    if (!child_pcb) return;

    child_pcb->state = XTCP_STATE_SYN_RECVD;
    child_pcb->event_cb = listen_pcb->event_cb; // 继承listen_pcb的回调，应用层传入http_handler
    child_pcb->local_port = listen_pcb->local_port; // 继承listen_pcb的端口，应用层传入80
    child_pcb->remote_ip = *remote_ip; // IP层传入
    child_pcb->remote_port = tcp_hdr->src_port;
    child_pcb->remote_win = tcp_hdr->window;
    child_pcb->ack = tcp_hdr->seq + 1; // SYN占用一个字节

    // 解析选项中的MSS
    tcp_read_mss(child_pcb, tcp_hdr);

    // 发送 SYN + ACK
    xnet_status_t status = tcp_send_segment(child_pcb, XTCP_FLAG_SYN | XTCP_FLAG_ACK);
    if (status < 0) {
        tcp_pcb_free(child_pcb);
    }
}

void xtcp_init(void) {
    // 整体清零，确保所有状态为 XTCP_STATE_FREE (0)
    memset(tcp_pcb_pool, 0, sizeof(tcp_pcb_pool));
}

void xtcp_in(xip_addr_t* remote_ip, xnet_packet_t* packet) {
    // 校验TCP包的长度
    if (packet->length < sizeof(xtcp_hdr_t)) {
        return;
    }
    // 校验校验和
    xtcp_hdr_t* tcp_hdr = (xtcp_hdr_t*) packet->data;
    uint16_t pre_checksum = tcp_hdr->checksum;
    tcp_hdr->checksum = 0;
    if (pre_checksum != 0) {
        uint16_t checksum = checksum_peso(remote_ip, &xnet_local_ip, XNET_PROTOCOL_TCP, (uint16_t*) tcp_hdr, packet->length);
        checksum = (checksum == 0) ? 0xFFFF : checksum;
        if (checksum != pre_checksum) {
            return;
        }
    }

    // 大小端转换
    tcp_hdr->src_port = swap_order16(tcp_hdr->src_port);
    tcp_hdr->dest_port = swap_order16(tcp_hdr->dest_port);
    tcp_hdr->hdr_flags.all = swap_order16(tcp_hdr->hdr_flags.all);
    tcp_hdr->seq = swap_order32(tcp_hdr->seq);
    tcp_hdr->ack = swap_order32(tcp_hdr->ack);
    tcp_hdr->window = swap_order16(tcp_hdr->window);

    // 查询五元组
    xtcp_pcb_t* pcb = xtcp_pcb_find(remote_ip, tcp_hdr->src_port, tcp_hdr->dest_port);
    if (pcb == NULL) {
        tcp_send_reset(tcp_hdr->seq + 1, tcp_hdr->dest_port, remote_ip, tcp_hdr->src_port);
        return;
    }

    // 收到第一次握手
    if (pcb->state == XTCP_STATE_LISTEN) {
        // 发送第二次握手
        tcp_process_accept(pcb, remote_ip, tcp_hdr);
        return;
    }

    if (tcp_hdr->seq != pcb->ack) {
        tcp_send_reset(tcp_hdr->seq + 1, tcp_hdr->dest_port, remote_ip, tcp_hdr->src_port);
        return;
    }

    // 这里可能是第三次握手，也可能是连接已建立后的正常通信，此时tcp_hdr可能包含option数据，所以不能使用sizeof(xtcp_hdr_t)
    remove_header(packet, tcp_hdr->hdr_flags.hdr_len * 4);
    uint16_t flags = tcp_hdr->hdr_flags.flags;
    switch (pcb->state) {
        case XTCP_STATE_SYN_RECVD:
            // 作为服务端，收到收到第三次握手
            if (flags & XTCP_FLAG_ACK) {
                // 确保对方确认的是我发的那个 SYN
                if (tcp_hdr->ack == pcb->next_seq) {
                    pcb->unacked_seq++; // 滑动窗口，确认第二次握手的请求，已收到
                    pcb->state = XTCP_STATE_ESTABLISHED;
                    pcb->event_cb(pcb, XTCP_EVENT_CONNECTED); // 通知应用层
                } else {
                    // ACK 号不对？可能是旧包，或者是攻击，忽略或发 RST
                }
            }
            break;
        case XTCP_STATE_ESTABLISHED:
            if ((flags & XTCP_FLAG_ACK)) {
                if ((pcb->unacked_seq < tcp_hdr->ack) && (tcp_hdr->ack <= pcb->next_seq)) {
                    uint16_t curr_ack_size = tcp_hdr->ack - pcb->unacked_seq;
                    tcp_buf_add_acked_count(&pcb->tx_buf, curr_ack_size);
                    pcb->unacked_seq += curr_ack_size;
                }
            }

            uint16_t read_size = tcp_recv(pcb, (uint8_t)tcp_hdr->hdr_flags.flags, packet->data, packet->length);

            // 作为服务端，收到第一次挥手
            if ((flags & XTCP_FLAG_FIN)) {
                pcb->state = XTCP_STATE_LAST_ACK;
                pcb->ack++;
                tcp_send_segment(pcb, XTCP_FLAG_FIN | XTCP_FLAG_ACK);
            }else if (read_size) {
                tcp_send_segment(pcb, XTCP_FLAG_ACK);
                pcb->event_cb(pcb, XTCP_EVENT_DATA_RECEIVED);
            }else if (tcp_buf_wait_send_count(&pcb->tx_buf)) { // 捎带发送
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
                pcb->ack++;
                tcp_send_segment(pcb,XTCP_FLAG_ACK);
                tcp_pcb_free(pcb);
            }
            break;
        case XTCP_STATE_LAST_ACK:
            if (flags & XTCP_FLAG_ACK) {
                pcb->event_cb(pcb, XTCP_EVENT_CLOSED);
                tcp_pcb_free(pcb);
            }
            break;
    }
}

// 新建一个pcb控制块
xtcp_pcb_t* xtcp_pcb_new(xtcp_event_handler_t handler) {
    // Alloc (分配内存)
    xtcp_pcb_t* pcb = xtcp_pcb_zalloc();
    if (!pcb) {
        return NULL;
    }

    // Init (协议初始化)
    xtcp_pcb_init(pcb);

    // Setup (用户自定义配置)
    pcb->event_cb = handler;

    return pcb;
}

xnet_status_t xtcp_pcb_bind(xtcp_pcb_t* pcb, uint16_t local_port) {
    if (pcb == NULL || local_port == 0) {
        return XNET_ERR_PARAM;
    }

    // 1. 检查端口是否已占用
    for (xtcp_pcb_t* curr = tcp_pcb_pool; curr < &tcp_pcb_pool[XTCP_PCB_MAX_NUM]; curr++) {
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
xtcp_pcb_t* xtcp_pcb_find(xip_addr_t* remote_ip, uint16_t remote_port, uint16_t local_port) {
    xtcp_pcb_t* listen_pcb = NULL;

    for (xtcp_pcb_t* curr = tcp_pcb_pool; curr < &tcp_pcb_pool[XTCP_PCB_MAX_NUM]; curr++) {
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

xnet_status_t xtcp_pcb_listen(xtcp_pcb_t* pcb) {
    if (pcb == NULL) return XNET_ERR_PARAM;

    // 只有处于 CLOSED 状态且已绑定端口的 pcb 才能开始 Listen
    if (pcb->state != XTCP_STATE_CLOSED) {
         return XNET_ERR_STATE;
    }
    if (pcb->local_port == 0) {
        return XNET_ERR_PARAM;
    }

    pcb->state = XTCP_STATE_LISTEN;
    return XNET_OK;
}

// 向pcb中写入数据
int xtcp_write(xtcp_pcb_t* pcb, uint8_t* data, uint16_t size) {
    int buffered_count;

    if ((pcb->state != XTCP_STATE_ESTABLISHED)) {
        return -1;
    }
    // 将数据拷贝到pcb->tx_buf，移动front
    buffered_count = tcp_buf_write(&pcb->tx_buf, data, size);
    if (buffered_count) {
        tcp_send_segment(pcb, XTCP_FLAG_ACK); // 只要连接建立，每一次发送都要带ACK
    }
    return buffered_count;
}

int xtcp_read(xtcp_pcb_t* pcb, uint8_t* data, uint16_t size) {
    return tcp_buf_read(&pcb->rx_buf, data, size);
}

// 服务端主动关闭连接
xnet_status_t xtcp_pcb_close(xtcp_pcb_t* pcb) {
    xnet_status_t status;

    if (pcb->state == XTCP_STATE_ESTABLISHED) {
        status = tcp_send_segment(pcb, XTCP_FLAG_FIN | XTCP_FLAG_ACK); // 只要连接已建立，必然带ACK
        if (status < 0) return status;
        pcb->state = XTCP_STATE_FIN_WAIT_1;
    } else {
        tcp_pcb_free(pcb);
    }
    return XNET_OK;
}