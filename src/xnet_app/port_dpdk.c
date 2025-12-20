/**
 * src/xnet_app/port_dpdk.c
 * Linux 平台下的 DPDK 驱动适配层
 */
#include "xnet_tiny.h"
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_cycles.h>

// DPDK 配置参数
#define RX_RING_SIZE 128
#define TX_RING_SIZE 512
#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 32

static uint16_t port_id = 0; // 默认使用第0个绑定的网卡
static struct rte_mempool *mbuf_pool = NULL;

// 协议栈需要的 IP 和 MAC (Linux 下)
const char *ip_str = "192.168.56.200"; // 注意：DPDK接管后 IP 由你自己定，要和 Windows Ping 的目标一致
// 真实网卡的 MAC (你需要改成你绑定给 DPDK 的那张网卡的 MAC)
// 根据你之前的截图，应该是 00:0c:29:c5:ec:62 (或者另外那张)
// 这里我们假设是 ens37 (02:05.0) 的 MAC
const char default_mac_addr[] = {0x00, 0x0c, 0x29, 0xc5, 0xec, 0x62};

/**
 * 初始化 DPDK 端口
 */
static int port_init(uint16_t port, struct rte_mempool *mbuf_pool) {
    struct rte_eth_conf port_conf = {0};
    const uint16_t rx_rings = 1, tx_rings = 1;
    uint16_t nb_rxd = RX_RING_SIZE;
    uint16_t nb_txd = TX_RING_SIZE;
    int retval;
    uint16_t q;
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

    // 开启混杂模式，确保能收到所有包
    rte_eth_promiscuous_enable(port);
    return 0;
}

/**
 * xnet_driver_open - 驱动初始化入口
 * 这里我们手动初始化 EAL
 */
xnet_status_t xnet_driver_open(uint8_t* mac_addr) {
    // 1. 伪造参数初始化 EAL
    // 注意：这里硬编码了参数，相当于你在命令行输入 ./splendid_net -l 0 --no-pci
    char *argv[] = {"splendid_net", "-l", "0", "--proc-type=auto"};
    int argc = 4;

    int ret = rte_eal_init(argc, argv);
    if (ret < 0) rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");

    // 2. 创建内存池
    mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS,
        MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());

    if (mbuf_pool == NULL) rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

    // 3. 检查是否有网卡可用
    if (rte_eth_dev_count_avail() == 0) {
        rte_exit(EXIT_FAILURE, "No DPDK ports available! Did you bind the NIC?\n");
    }

    // 4. 初始化端口 0
    if (port_init(port_id, mbuf_pool) != 0) {
        rte_exit(EXIT_FAILURE, "Cannot init port %"PRIu16 "\n", port_id);
    }

    // 5. 将写死的 MAC 地址传回给协议栈
    memcpy(mac_addr, default_mac_addr, 6);

    printf("DPDK Driver Initialized on Port %d\n", port_id);
    return XNET_OK;
}

/**
 * 发送包：把协议栈的 buffer 拷贝到 DPDK mbuf 并发送
 */
xnet_status_t xnet_driver_send(xnet_packet_t* packet) {
    struct rte_mbuf *m;

    // 1. 申请一个 DPDK mbuf
    m = rte_pktmbuf_alloc(mbuf_pool);
    if (m == NULL) return XNET_ERR_IO;

    // 2. 数据拷贝 (协议栈 -> DPDK)
    // packet->data 是你的数据，packet->length 是长度
    void *d = rte_pktmbuf_append(m, packet->length);
    if (d == NULL) {
        rte_pktmbuf_free(m);
        return XNET_ERR_IO;
    }
    rte_memcpy(d, packet->data, packet->length);

    // 3. 发送
    uint16_t nb_tx = rte_eth_tx_burst(port_id, 0, &m, 1);

    if (nb_tx < 1) {
        rte_pktmbuf_free(m); // 发送失败要手动释放
        return XNET_ERR_IO;
    }

    return XNET_OK;
}

/**
 * 接收包：从 DPDK 收包并拷贝到协议栈 buffer
 */
xnet_status_t xnet_driver_read(xnet_packet_t** packet) {
    struct rte_mbuf *bufs[BURST_SIZE];

    // 1. 尝试收取一批包
    uint16_t nb_rx = rte_eth_rx_burst(port_id, 0, bufs, BURST_SIZE);

    if (nb_rx == 0) return XNET_ERR_IO; // 没收到

    // 2. 目前协议栈每次只处理一个包，所以我们只取第一个
    // (优化的做法是做一个队列，但在 simple 模式下我们先处理第一个，其他的丢掉或暂存)
    struct rte_mbuf *m = bufs[0];

    // 3. 申请协议栈的接收包内存
    xnet_packet_t* r_packet = xnet_alloc_rx_packet(rte_pktmbuf_data_len(m));

    // 4. 数据拷贝 (DPDK -> 协议栈)
    rte_memcpy(r_packet->data, rte_pktmbuf_mtod(m, void*), rte_pktmbuf_data_len(m));
    r_packet->length = rte_pktmbuf_data_len(m);

    // 5. 释放所有收到的 DPDK mbuf (因为数据已经拷走了)
    for (int i = 0; i < nb_rx; i++) {
        rte_pktmbuf_free(bufs[i]);
    }

    *packet = r_packet;
    return XNET_OK;
}

/**
 * Linux 下的高精度时间获取
 */
const xnet_time_t xsys_get_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec;
}