#include "xserver_http.h"
#include <stdio.h>
#include <string.h>

#include "xnet_tcp.h"
#include "xnet_tiny.h"

// FIFO 队列长度，与 PCB 数量一致
#define XHTTP_FIFO_SIZE XTCP_PCB_MAX_NUM

#define XHTTP_DOC_PATH "D:\\Develop\\IdeaProject\\Learning-DIY-TCPIP_WEB-master\\htdocs"

static char rx_buffer[1024], tx_buffer[1024];
static char url_path[255], file_path[255];

typedef struct _xhttp_fifo_t {
    xtcp_pcb_t* clients[XHTTP_FIFO_SIZE];
    uint8_t read_idx, write_idx;
} xhttp_fifo_t;

static xhttp_fifo_t http_fifo;

static int get_line(xtcp_pcb_t* pcb, char* buf, int size) {
    int i = 0;

    while (i < size) {
        char c;

        if (xtcp_recv(pcb, (uint8_t*)&c, 1) > 0) {
            if ((c != '\r') && (c != '\n')) {
                buf[i++] = c;
            }
            else if (c == '\n') {
                break;
            }
        }
        xnet_poll();
    }

    buf[i] = '\0';
    return i;
}

static void xhttp_fifo_init(xhttp_fifo_t* fifo) {
    fifo->write_idx = 0; //写指针，队尾
    fifo->read_idx = 0;  //读指针，队头
}

static xnet_status_t xhttp_fifo_enqueue(xhttp_fifo_t* fifo, xtcp_pcb_t* pcb) {
    // 1. 预判 tail 的下一步位置
    int next_write = (fifo->write_idx + 1) % XHTTP_FIFO_SIZE;

    // 2. 判满逻辑：如果下一步撞上了 read，说明只剩这一个保留空位了
    // 此时队列里实际存了 SIZE-1 个元素，刚好对应所有可用的39个 PCB
    if (next_write == fifo->read_idx) {
        return XNET_ERR_MEM;
    }

    fifo->clients[fifo->write_idx] = pcb;
    fifo->write_idx = next_write; // 更新 tail

    return XNET_OK;
}

static xtcp_pcb_t* xhttp_fifo_dequeue(xhttp_fifo_t* fifo) {
    // 1. 判空逻辑：头尾重合就是空
    if (fifo->read_idx == fifo->write_idx) {
        return NULL;
    }

    xtcp_pcb_t* client = fifo->clients[fifo->read_idx];
    fifo->read_idx = (fifo->read_idx + 1) % XHTTP_FIFO_SIZE;

    return client;
}

// [新增] 动态计算当前队列长度
static int xhttp_fifo_count(xhttp_fifo_t* fifo) {
    return (fifo->write_idx - fifo->read_idx + XHTTP_FIFO_SIZE) % XHTTP_FIFO_SIZE;
}

// 应用层回调方法
static xnet_status_t http_handler(xtcp_pcb_t* pcb, xtcp_event_t event) {

    switch (event) {
        case XTCP_EVENT_CONNECTED:
            printf("http: new client connected from %d.%d.%d.%d:%d\n",
               pcb->remote_ip.addr[0],
               pcb->remote_ip.addr[1],
               pcb->remote_ip.addr[2],
               pcb->remote_ip.addr[3],
               pcb->remote_port);
            xhttp_fifo_enqueue(&http_fifo, pcb);
            printf("fifo queue length: %d\n", xhttp_fifo_count(&http_fifo));
            break;

        case XTCP_EVENT_SENT:
            break;

        case XTCP_EVENT_DATA_RECEIVED:
            break;

        case XTCP_EVENT_CLOSED:
            printf("http: connection closed from %d.%d.%d.%d:%d\n",
                   pcb->remote_ip.addr[0],
                   pcb->remote_ip.addr[1],
                   pcb->remote_ip.addr[2],
                   pcb->remote_ip.addr[3],
                   pcb->remote_port);
            break;

        default:
            break;
    }
    return XNET_OK;
}

static void close_http(xtcp_pcb_t* pcb) {
    xtcp_pcb_close(pcb);
    printf("http closed.\n");
}

static int http_send(xtcp_pcb_t* tcp, char* buf, int size) {
    int sended_size = 0;
    while (size > 0) {
        // 调用底层写函数
        int curr_size = xtcp_send(tcp, (uint8_t*)buf, (uint16_t)size);
        if (curr_size < 0) return; // 发送异常则退出

        size -= curr_size;
        buf += curr_size;
        sended_size += curr_size;
        xnet_poll();
    }
    return sended_size;
}

static void send_404_not_found(xtcp_pcb_t* tcp) {
    sprintf(tx_buffer, "HTTP/1.0 404 NOT FOUND\r\n\r\n");
    http_send(tcp, tx_buffer, strlen(tx_buffer));
}

// 发送文件逻辑
static void send_file(xtcp_pcb_t* pcb, const char* url) {
    FILE* file;
    uint32_t size;

    // 处理路径：跳过开头的 '/'
    while (*url == '/') url++;
    sprintf(file_path, "%s/%s", XHTTP_DOC_PATH, url);

    // 以二进制只读方式打开文件
    file = fopen(file_path, "rb");
    if (file == NULL) {
        send_404_not_found(pcb);
        return;
    }

    // 获取文件大小
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // 构造响应头
    sprintf(tx_buffer,
        "HTTP/1.0 200 OK\r\n"
        "Content-Length:%d\r\n"
        "\r\n",
        size
    );
    http_send(pcb, tx_buffer, strlen(tx_buffer));

    // 循环读取文件内容并发送
    while (!feof(file)) {
        size = fread(tx_buffer, 1, sizeof(tx_buffer), file);
        if (http_send(pcb, tx_buffer, size) < 0) {
            fclose(file); // 异常退出前关闭文件
            return;
        }
    }

    // 完成后关闭文件
    fclose(file);
}

xnet_status_t xserver_http_create(uint16_t port) {
    xtcp_pcb_t* pcb = xtcp_pcb_new(http_handler);
    xtcp_pcb_bind(pcb, port);
    xtcp_pcb_listen(pcb);
    xhttp_fifo_init(&http_fifo);
    return XNET_OK;
}

void xserver_http_run(void) {
    xtcp_pcb_t* pcb;

    while ((pcb = xhttp_fifo_dequeue(&http_fifo)) != NULL) {
        int i;
        char* c = rx_buffer;
        if (get_line(pcb, rx_buffer, sizeof(rx_buffer)) < 0) {
            close_http(pcb);
            continue;
        }

        // 检查是否为 GET 请求
        if (strncmp(rx_buffer, "GET", 3) != 0) {
            close_http(pcb);
            continue;
        }

        // 跳过 "GET" 之后的空格
        while (*c != ' ') c++;
        while (*c == ' ') c++;

        // 提取 URL 路径到 url_path 数组
        for (i = 0; i < sizeof(url_path); i++) {
            if (*c == ' ') break;
            url_path[i] = *c++;
        }
        url_path[i] = '\0';
        // 如果路径以 '/' 结尾，自动追加 "index.html"
        if (url_path[strlen(url_path) - 1] == '/') {
            strcat(url_path, "index.html");
        }

        send_file(pcb, url_path);
        close_http(pcb);
    }
}
