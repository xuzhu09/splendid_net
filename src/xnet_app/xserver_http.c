#include "xserver_http.h"

#include "xsocket.h"
#include "xnet_tiny.h"
#include <stdio.h>
#include <string.h>

#if defined(_WIN32) || defined(_WIN64)
    // Windows 环境下的路径
    #define XHTTP_DOC_ROOT  "D:\\Develop\\IdeaProject\\Learning-DIY-TCPIP_WEB-master\\htdocs"
#else
    // Linux 环境下的路径 (假设你把它放在工程根目录下的 htdocs 文件夹里)
    #define XHTTP_DOC_ROOT  "/home/efairy520/splendid_net/htdocs"
#endif

static char xhttp_recv_buf[1024];
static char xhttp_send_buf[1024];
static char xhttp_req_path[255];
static char xhttp_fs_path[255];

// 全局 Server Socket
static xsocket_t *server_socket;

// -------------------------------------------------------------------------
// 辅助函数
// -------------------------------------------------------------------------

// 基于 Socket 的按行读取
static int xhttp_read_line(xsocket_t *sock, char *buf, int max_len) {
    int i = 0;

    // 1. 记录刚进来的绝对物理时间 (比如毫秒时间戳)
    xnet_time_t start_time = xsys_get_time();

    while (i < max_len - 1) {
        char c;
        int len = xsocket_read(sock, &c, 1);

        if (len < 0) {
            return -1; // 对方真断开了
        }

        if (len == 0) {
            // 2. 检查距离上次收到数据，物理时间是否超过了 3 秒
            if (xnet_time_check_tmo(&start_time, 3)) {
                printf("[Warn] Client idle for 3 seconds, kicking out!\n");
                return -1; // 强制踢出
            }
            continue;
        }

        // 3. 只要收到哪怕 1 个字节，立刻重置倒计时炸弹！(类似于串口超时断帧)
        start_time = xsys_get_time();

        if (c != '\r' && c != '\n') {
            buf[i++] = c;
        } else if (c == '\n') {
            break;
        }
    }
    buf[i] = '\0';
    return i;
}

static void xhttp_send_404(xsocket_t *sock) {
    sprintf(xhttp_send_buf, "HTTP/1.0 404 NOT FOUND\r\n\r\n");
    xsocket_write(sock, xhttp_send_buf, strlen(xhttp_send_buf));
}

static void xhttp_send_file(xsocket_t *sock, const char *url_path) {
    FILE *file;
    uint32_t file_size;

    while (*url_path == '/') url_path++;
    sprintf(xhttp_fs_path, "%s/%s", XHTTP_DOC_ROOT, url_path);

    file = fopen(xhttp_fs_path, "rb");
    if (file == NULL) {
        xhttp_send_404(sock);
        return;
    }

    fseek(file, 0, SEEK_END);
    file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    sprintf(xhttp_send_buf,
        "HTTP/1.0 200 OK\r\n"
        "Server: XSocket-Http/1.0\r\n"
        "Connection: close\r\n"         //传完这个文件我就要关 TCP 了
        "Content-Length: %d\r\n"
        "\r\n",
        file_size
    );
    xsocket_write(sock, xhttp_send_buf, strlen(xhttp_send_buf));

    while (!feof(file)) {
        size_t read_len = fread(xhttp_send_buf, 1, sizeof(xhttp_send_buf), file);
        if (read_len > 0) {
            xsocket_write(sock, xhttp_send_buf, (int)read_len);
        }
    }
    fclose(file);
}

// -------------------------------------------------------------------------
// 业务逻辑
// -------------------------------------------------------------------------

static void xhttp_handle_client(xsocket_t *client) {
    // 1. 读取请求行
    if (xhttp_read_line(client, xhttp_recv_buf, sizeof(xhttp_recv_buf)) <= 0) {
        return;
    }

    // 2. 校验 GET
    if (strncmp(xhttp_recv_buf, "GET", 3) != 0) {
        return;
    }

    // 3. 解析路径
    char *c = xhttp_recv_buf;
    while (*c != ' ') c++;
    while (*c == ' ') c++;

    int i;
    for (i = 0; i < sizeof(xhttp_req_path) - 1; i++) {
        if (*c == ' ' || *c == '\0') break;
        xhttp_req_path[i] = *c++;
    }
    xhttp_req_path[i] = '\0';

    if (xhttp_req_path[strlen(xhttp_req_path) - 1] == '/') {
        strcat(xhttp_req_path, "index.html");
    }

    // 4. 发送文件
    printf("[Http Server]: request %s\n", xhttp_req_path);
    xhttp_send_file(client, xhttp_req_path);
}

// -------------------------------------------------------------------------
// 对外接口
// -------------------------------------------------------------------------

xnet_status_t xhttp_server_create(uint16_t port) {
    server_socket = xsocket_open(XSOCKET_TYPE_TCP);
    if (!server_socket) return XNET_ERR_MEM;

    xsocket_bind(server_socket, port);
    xsocket_listen(server_socket);

    return XNET_OK;
}

void xhttp_server_poll(void) {
    // 从backlog队列pop一个client
    xsocket_t *client = xsocket_accept(server_socket);

    if (client) {
        // 有人连上来了，就处理
        xhttp_handle_client(client);

        // 处理完，关闭连接
        xsocket_close(client);
    }
}