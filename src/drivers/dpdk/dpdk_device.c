/**
 * src/drivers/dpdk/dpdk_device.c
 * 纯粹的 DPDK 驱动层，不懂业务，只懂搬运数据
 */
#include "dpdk_device.h"
#include <stdio.h>
#include <string.h>

// 宏定义搬过来
#define RX_RING_SIZE 128
#define TX_RING_SIZE 512
#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 32

static uint16_t port_id = 0;
static struct rte_mempool *mbuf_pool = NULL;
extern int xnet_cfg_hw_csum;

// 你的硬编码 MAC 地址
const char default_mac_addr[] = {0x00, 0x0c, 0x29, 0xc5, 0xec, 0x62};

// 内部辅助函数：初始化端口 (逻辑不变，照搬你的 code)
static int port_init(uint16_t port, struct rte_mempool *mbuf_pool) {
    struct rte_eth_conf port_conf = {0};
    const uint16_t rx_rings = 1, tx_rings = 1;
    uint16_t nb_rxd = RX_RING_SIZE;
    uint16_t nb_txd = TX_RING_SIZE;
    int retval;
    struct rte_eth_dev_info dev_info;
    struct rte_eth_txconf txconf;

    if (!rte_eth_dev_is_valid_port(port)) return -1;

    retval = rte_eth_dev_info_get(port, &dev_info);
    if (retval != 0) return retval;

    if (dev_info.tx_offload_capa & RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE)
        port_conf.txmode.offloads |= RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE;

    retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
    if (retval != 0) return retval;

    retval = rte_eth_rx_queue_setup(port, 0, nb_rxd, rte_eth_dev_socket_id(port), NULL, mbuf_pool);
    if (retval < 0) return retval;

    txconf = dev_info.default_txconf;
    txconf.offloads = port_conf.txmode.offloads;
    retval = rte_eth_tx_queue_setup(port, 0, nb_txd, rte_eth_dev_socket_id(port), &txconf);
    if (retval < 0) return retval;

    retval = rte_eth_dev_start(port);
    if (retval < 0) return retval;

    rte_eth_promiscuous_enable(port);
    return 0;
}

// 1. 初始化接口
int dpdk_device_init(void) {
    // 你的硬编码参数
    char *argv[] = {"splendid_net", "-l", "0", "--proc-type=auto"};
    int argc = 4;

    int ret = rte_eal_init(argc, argv);
    if (ret < 0) return DPDK_ERR;

    mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS,
        MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());

    if (mbuf_pool == NULL) return DPDK_ERR;

    if (rte_eth_dev_count_avail() == 0) return DPDK_ERR;

    if (port_init(port_id, mbuf_pool) != 0) return DPDK_ERR;
    xnet_cfg_hw_csum = 1;
    printf(">> [DPDK Driver] Initialized on Port %d\n", port_id);
    return DPDK_OK;
}

// 2. 发送接口 (接收纯数据指针)
int dpdk_device_send(const void *data, uint16_t len) {
    struct rte_mbuf *m;

    m = rte_pktmbuf_alloc(mbuf_pool);
    if (m == NULL) return 0;

    void *d = rte_pktmbuf_append(m, len);
    if (d == NULL) {
        rte_pktmbuf_free(m);
        return 0;
    }
    rte_memcpy(d, data, len);

    uint16_t nb_tx = rte_eth_tx_burst(port_id, 0, &m, 1);
    if (nb_tx < 1) {
        rte_pktmbuf_free(m);
        return 0;
    }
    return nb_tx;
}

// 3. 接收接口 (严格单包读取，杜绝丢包黑洞)
int dpdk_device_read(void *buffer, uint16_t max_len) {
    // 【关键修改】：每次只申请接收 1 个包，不要用 BURST_SIZE 贪多
    struct rte_mbuf *bufs[1];
    uint16_t nb_rx = rte_eth_rx_burst(port_id, 0, bufs, 1);

    // 没收到包直接返回
    if (nb_rx == 0) return 0;

    // 现在 nb_rx 必定是 1，安全处理这唯一的一个包
    struct rte_mbuf *m = bufs[0];
    uint16_t pkt_len = rte_pktmbuf_data_len(m);

    // 防止溢出
    if (pkt_len > max_len) pkt_len = max_len;

    // 拷贝数据给协议栈
    rte_memcpy(buffer, rte_pktmbuf_mtod(m, void*), pkt_len);

    // 释放这个已经被处理完的包
    rte_pktmbuf_free(m);

    return pkt_len;
}

// 4. 获取 MAC
void dpdk_device_get_mac(uint8_t *mac_buf) {
    // 既然你之前是硬编码的，这里也直接 copy 硬编码的 MAC
    rte_memcpy(mac_buf, default_mac_addr, 6);

    // 如果你想从真实硬件读取，可以用这个：
    // struct rte_ether_addr mac_addr;
    // rte_eth_macaddr_get(port_id, &mac_addr);
    // rte_memcpy(mac_buf, mac_addr.addr_bytes, 6);
}