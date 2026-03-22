set(PORT_SRCS "")

# -------------------------------------------------------
# 根据全局环境变量，精准选择适配层文件和底层驱动库
# -------------------------------------------------------
if(XNET_ENV STREQUAL "windows")
    list(APPEND PORT_SRCS netif_pcap.c)
    set(DRIVER_LIB driver_pcap)

elseif(XNET_ENV STREQUAL "ubuntu")
    list(APPEND PORT_SRCS netif_dpdk.c)
    set(DRIVER_LIB driver_dpdk)

elseif(XNET_ENV STREQUAL "debian")
    list(APPEND PORT_SRCS netif_tap.c)
    set(DRIVER_LIB driver_tap)
endif()

# 1. 编译为静态库
add_library(xnet_netif STATIC ${PORT_SRCS})

# 2. 设置头文件路径
target_include_directories(xnet_netif
        PUBLIC .
        PRIVATE ../core
)

# 3. 链接对应的底层网卡驱动
target_link_libraries(xnet_netif PRIVATE ${DRIVER_LIB})