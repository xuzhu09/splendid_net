//
// Created by efairy520 on 2025/12/7.
//

#ifndef XNET_UDP_H
#define XNET_UDP_H

#include "xnet_def.h"
#include "xnet_ip.h"

#define XUDP_MAX_PCB_COUNT 10

typedef struct _xudp_pcb_t xudp_pcb_t;

// UDP 处理回调函数定义，类似于java中的接口，由业务类提供实现
typedef xnet_status_t (*xudp_handler_t) (xudp_pcb_t *pcb, xip_addr_t *src_ip, uint16_t src_port, xnet_packet_t *packet);

void xudp_init(void);

xudp_pcb_t *xudp_alloc_pcb(xudp_handler_t handler);
xnet_status_t xudp_bind_pcb(xudp_pcb_t *pcb, uint16_t port);
void xudp_free_pcb(xudp_pcb_t *pcb);

void xudp_in(xnet_packet_t *packet, xip_addr_t *src_ip, xip_addr_t *dest_ip, xip_hdr_t *orig_ip_hdr);
xnet_status_t xudp_send_to(xudp_pcb_t *pcb, xip_addr_t *dest_ip, uint16_t dest_port, xnet_packet_t *packet);

#endif //XNET_UDP_H