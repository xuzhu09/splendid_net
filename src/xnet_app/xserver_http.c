#include "xserver_http.h"
#include <stdio.h>
#include "xnet_tcp.h"

// 准备 4096 字节的数据 (故意比 2048 缓冲区大)
#define TEST_DATA_LEN 4096
static uint8_t tx_buffer[TEST_DATA_LEN];

// 记录发送进度的变量
static int sent_len = 0;

// 尝试发送数据
static void try_send_data(xtcp_pcb_t* pcb) {
    // 算出还剩多少没发
    int remaining_len = TEST_DATA_LEN - sent_len;

    if (remaining_len <= 0) {
        return; // 发完了
    }

    // 将剩余的数据写入 TCP 缓冲区
    int written = xtcp_write(pcb, tx_buffer + sent_len, remaining_len);

    // 更新进度条
    if (written > 0) {
        sent_len += written;
        printf(">> Progress: Sent %d bytes, Total: %d / %d\n", written, sent_len, TEST_DATA_LEN);
    } else {
        printf(">> Buffer full! Waiting for XTCP_EVENT_SENT...\n");
    }

    // 如果全部发完，可以考虑关闭连接（可选）
    if (sent_len >= TEST_DATA_LEN) {
        printf(">> All data sent successfully!\n");
        // xtcp_pcb_close(pcb);
    }
}

// 应用层回调方法
static xnet_status_t http_handler(xtcp_pcb_t* pcb, xtcp_event_t event) {
    // 初始化测试数据 "0123..."
    static char num[] = "0123456789ABCDEF";
    // 只初始化一次数据
    static int data_inited = 0;
    if (!data_inited) {
        for (int i = 0; i < TEST_DATA_LEN; i++) {
            tx_buffer[i] = num[i % 16];
        }
        data_inited = 1;
    }

    switch (event) {
        case XTCP_EVENT_CONNECTED:
            printf("http: client connected. Start sending %d bytes...\n", TEST_DATA_LEN);
            sent_len = 0; // 【重置进度】非常重要！

            // 连接刚建立，缓冲区肯定是空的，立刻尝试发第一波
            try_send_data(pcb);
            break;

        case XTCP_EVENT_SENT:
            // 【核心】：收到这个事件，说明对方回 ACK 了，缓冲区有空位了
            // 赶紧接着发剩下的！
            try_send_data(pcb);
            break;

        case XTCP_EVENT_DATA_RECEIVED:
            // 暂时不管接收
            break;

        case XTCP_EVENT_CLOSED:
            printf("http: connection closed\n");
            break;

        default:
            break;
    }
    return XNET_OK;
}

xnet_status_t xserver_http_create(uint16_t port) {
    xtcp_pcb_t* pcb = xtcp_pcb_new(http_handler);
    xtcp_pcb_bind(pcb, port);
    xtcp_pcb_listen(pcb);
    return XNET_OK;
}