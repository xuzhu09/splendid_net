#ifndef XSOCKET_H
#define XSOCKET_H

#include <stdint.h>
#include "xnet_def.h"

// Windows DLL 导出宏
// 如果在 Linux 下编译，这个宏通常定义为空
#ifndef XNET_EXPORT
    #ifdef _WIN32
        #define XNET_EXPORT __declspec(dllexport)
    #else
        #define XNET_EXPORT
    #endif
#endif

typedef struct _xsocket_t xsocket_t;

typedef enum {
    XSOCKET_TYPE_TCP = 0,
    XSOCKET_TYPE_UDP = 1,
} xsocket_type_t;

// ===== 打开/关闭 =====
XNET_EXPORT xsocket_t *xsocket_open(xsocket_type_t type);
XNET_EXPORT void xsocket_close(xsocket_t *socket);

// ===== 通用：绑定 =====
XNET_EXPORT xnet_status_t xsocket_bind(xsocket_t *socket, uint16_t port);

// ===== TCP 专用 =====
XNET_EXPORT xnet_status_t xsocket_listen(xsocket_t *socket, uint8_t backlog);
XNET_EXPORT xsocket_t *xsocket_accept(xsocket_t *socket);

XNET_EXPORT int xsocket_write(xsocket_t *socket, const char *data, int len);

XNET_EXPORT int xsocket_try_read(xsocket_t *socket, char *buf, int max_len);
XNET_EXPORT int xsocket_read_timeout(xsocket_t *socket, char *buf, int max_len, int max_polls);
XNET_EXPORT int xsocket_read(xsocket_t *socket, char *buf, int max_len);

// ===== UDP 专用 =====
XNET_EXPORT int xsocket_sendto(xsocket_t *socket, const char *data, int len,
                   const xip_addr_t *dest_ip, uint16_t dest_port);

// 返回：>0 收到的字节数；0 超时；-1 参数/状态错误
XNET_EXPORT int xsocket_recvfrom(xsocket_t *socket, char *buf, int max_len,
                     xip_addr_t *src_ip, uint16_t *src_port, int max_polls);

#endif // XSOCKET_H
