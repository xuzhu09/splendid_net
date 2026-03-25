# src/xnet_tiny/CMakeLists.txt

# 1. 纯净的核心源文件 (不含任何平台相关的 .c 文件)
set(TINY_SRCS
        # 核心层
        core/xnet_tiny.c

        # 链路层
        link/xnet_ethernet.c
        link/xnet_arp.c

        # 网络层
        network/xnet_ip.c
        network/xnet_icmp.c

        # 传输层
        transport/xnet_udp.c
        transport/xnet_tcp.c

        # 基础设施层
        app/xnet_dhcp.c
)

# 2. 创建静态库 (推荐做成静态库)
add_library(xnet_tiny STATIC ${TINY_SRCS})

# 3. 激活子目录，让 CMake 进去执行里面的 CMakeLists.txt
add_subdirectory(arch)
add_subdirectory(netif)

# 4. 把 arch 和 netif 的静态库，“吸附”到主协议栈上
# 这样外面只要链接 xnet_tiny，就会自动带上对应的跨平台实现
target_link_libraries(xnet_tiny PRIVATE xnet_arch xnet_netif)

# 5. 设置头文件搜索路径
# 这一步非常关键！它把所有子目录都加到了搜索路径里。
target_include_directories(xnet_tiny
        PUBLIC
        # 对外公开 core，因为外部需要引用 xnet_tiny.h 和 xnet_def.h
        ${CMAKE_CURRENT_SOURCE_DIR}/core

        PRIVATE
        # 内部模块之间互相引用，对外隐藏
        ${CMAKE_CURRENT_SOURCE_DIR}/link
        ${CMAKE_CURRENT_SOURCE_DIR}/network
        ${CMAKE_CURRENT_SOURCE_DIR}/transport
        # 承认依赖：告诉 core 去哪里找网卡和时间的头文件
        ${CMAKE_CURRENT_SOURCE_DIR}/netif
        ${CMAKE_CURRENT_SOURCE_DIR}/arch
)