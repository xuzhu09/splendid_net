cmake_minimum_required(VERSION 3.16)
project(splendid_net C)

# =======================================================
# 🚀 核心修复：在这里全局开启 CPU 指令集支持
# 放在最前面，确保 xnet_app, xnet_tiny 等所有子库都能吃到这个配置
# =======================================================
if(NOT WIN32)
    add_compile_options(-march=native)
endif()

set(CMAKE_C_STANDARD 99)

# 1. 包含路径
include_directories(
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/src/xnet_app
        ${PROJECT_SOURCE_DIR}/src/xnet_tiny
        ${PROJECT_SOURCE_DIR}/lib/xnet
)

# 2. 自动发现 DPDK (Linux 专用)
if(NOT WIN32)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(DPDK REQUIRED libdpdk)
    include_directories(${DPDK_INCLUDE_DIRS})
    link_directories(${DPDK_LIBRARY_DIRS})
    add_definitions(-DALLOW_EXPERIMENTAL_API) # DPDK 必须的宏
endif()

# 3. 核心：根据系统选择不同的“驱动文件”
if(WIN32)
    # Windows 使用 Pcap
    set(DRIVER_SOURCE
            src/xnet_app/port_pcap.c
            lib/xnet/pcap_device.c
    )
    # Windows Npcap 设置 (保持你原来的不变)
    set(NPCAP_SDK_DIR "C:/Users/efair/Downloads/npcap-sdk-1.15")
    include_directories(${NPCAP_SDK_DIR}/Include)
    link_directories(${NPCAP_SDK_DIR}/Lib/x64)
else()
    # Linux 使用 DPDK (新建这个文件!)
    set(DRIVER_SOURCE src/xnet_app/port_dpdk.c)
endif()

# 4. 生成可执行文件 (注意这里用变量 DRIVER_SOURCE)
add_executable(${PROJECT_NAME}
        src/app.c
        ${DRIVER_SOURCE}
)

# 5. 添加子模块
add_subdirectory(src/xnet_tiny)
add_subdirectory(src/xnet_app)

# 6. 链接库
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE
            xnet_tiny
            xnet_app
            wpcap
            Packet
            Ws2_32
    )
else()
    # -------- Linux (Ubuntu) 链接逻辑 --------
    target_link_libraries(${PROJECT_NAME} PRIVATE
            xnet_tiny       # 协议栈核心
            xnet_app        # <--- 【重点】这里补上了你的业务代码库！
            -Wl,--whole-archive ${DPDK_LIBRARIES} -Wl,--no-whole-archive
            pthread
            numa
            dl
            m
    )
endif()