//
// Created by efairy520 on 2025/12/12.
//

#include "xserver_http.h"
#include <stdio.h>
#include "xnet_tcp.h"

static uint8_t tx_buffer[1024];

static xnet_status_t http_handler(xtcp_pcb_t* pcb, xtcp_event_t event) {
    static char num[] = "0123456789ABCDEF";

    switch (event) {
        case XTCP_EVENT_CONNECTED:
            printf("http: new client connected\n");
            // xtcp_pcb_close(pcb);
            /*
            // 构造一个1024长度的字符串
            for (int i = 0; i < 1024; i++) {
                tx_buffer[i] = num[i % 16];
            }
            // 将1024长度的字符串拷贝到pcb
            xtcp_write(pcb, tx_buffer, sizeof(tx_buffer));
            */
            break;
        case XTCP_EVENT_DATA_RECEIVED:
            // 收到数据，直接echo
            uint8_t* data = tx_buffer;
            uint16_t read_size = xtcp_read(pcb, tx_buffer, sizeof(tx_buffer));
            while (read_size) {
                uint16_t curr_size = xtcp_write(pcb, data, read_size);
                data += curr_size;
                read_size -= curr_size;
            }
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
