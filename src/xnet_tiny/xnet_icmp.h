//
// Created by efairy520 on 2025/12/5.
//

#ifndef XNET_ICMP_H
#define XNET_ICMP_H

#include "xnet_ip.h"
#include "xnet_def.h"

#pragma pack(1) // 必须强制 1 字节对齐，防止编译器乱填空隙
typedef struct _xicmp_hdr_t {
    uint8_t type;       // 类型：8=Request, 0=Reply
    uint8_t code;       // 代码：0
    uint16_t checksum;  // 校验和
    uint16_t id;        // 标识符 (不要转字节序，当成不透明数据)
    uint16_t seq;       // 序列号 (不要转字节序，当成不透明数据)
    // 后面紧接着就是 Data，用指针访问即可
} xicmp_hdr_t;
#pragma pack()

#define XICMP_CODE_ECHO_REQUEST     8
#define XICMP_CODE_ECHO_REPLY       0

#define XICMP_TYPE_UNREACH          3
#define XICMP_CODE_PORT_UNREACH     3
#define XICMP_CODE_PRO_UNREACH      2

void xicmp_init(void);
void xicmp_in(xip_addr_t* src_ip, xnet_packet_t* packet);
xnet_status_t xicmp_dest_unreach(uint8_t code, xip_hdr_t* ip_hdr) ;

#endif //XNET_ICMP_H
