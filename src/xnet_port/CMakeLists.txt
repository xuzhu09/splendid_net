# 1. 自动根据平台选择源文件
set(PORT_SRCS "")

if(WIN32)
    list(APPEND PORT_SRCS
            port_pcap.c
            ${PROJECT_SOURCE_DIR}/lib/xnet/pcap_device.c
    )
else()
    list(APPEND PORT_SRCS port_dpdk.c)
endif()

# 2. 生成 OBJECT 库
add_library(xnet_port OBJECT ${PORT_SRCS})

# 3. 设置头文件路径
target_include_directories(xnet_port
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        PRIVATE
        ${PROJECT_SOURCE_DIR}/lib/xnet
        ${PROJECT_SOURCE_DIR}/src/xnet_tiny
)

# 4. 链接外部 SDK (关键！这样 port_pcap.c 才能找到 pcap.h)
if(WIN32)
    target_link_libraries(xnet_port PRIVATE Npcap::SDK)
else()
    target_link_libraries(xnet_port PRIVATE DPDK::SDK)
endif()