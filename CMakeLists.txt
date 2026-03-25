cmake_minimum_required(VERSION 3.16)
project(splendid_net C)

# =======================================================
# 🌟 终极补丁：强制所有代码生成“位置无关代码” (-fPIC)
# 这是 Linux 下把静态库揉进 .so 的唯一合法通行证！
# =======================================================
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_C_STANDARD 99)

# =======================================================
# 1. 弹药库输出路径配置 (保证生成的 .so 和 .dll 都在根目录下)
# =======================================================
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

if(NOT WIN32)
    add_compile_options(-march=native)
endif()

# =======================================================
# 2. 跨平台网卡驱动配置：Windows(Npcap) vs Ubuntu(DPDK) vs Debian(TAP)
# =======================================================
# 如果没有在 CLion 里手动指定环境，系统自动兜底猜测
if(NOT DEFINED XNET_ENV)
    if(WIN32)
        set(XNET_ENV "windows")
    else()
        set(XNET_ENV "debian") # Linux 下默认走安全的 TAP 环境
    endif()
endif()

message(STATUS "========== 🚀 XNET TARGET ENV: ${XNET_ENV} ==========")

if(XNET_ENV STREQUAL "windows")
    # --- Windows Npcap 配置 ---
    set(ENV_LOCAL_IP "192, 168, 254, 2")
    set(NPCAP_SDK_DIR "C:/Users/efair/Downloads/npcap-sdk-1.15")

    add_library(Npcap::SDK UNKNOWN IMPORTED)
    set_target_properties(Npcap::SDK PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${NPCAP_SDK_DIR}/Include"
            IMPORTED_LOCATION "${NPCAP_SDK_DIR}/Lib/x64/wpcap.lib"
            INTERFACE_LINK_LIBRARIES "${NPCAP_SDK_DIR}/Lib/x64/Packet.lib;Ws2_32"
    )

    set(DRIVER_TARGET Npcap::SDK)
    set(DRIVER_SUBDIR "src/drivers/pcap")

elseif(XNET_ENV STREQUAL "ubuntu")
    # --- Linux DPDK 配置 ---
    set(ENV_LOCAL_IP "192, 168, 56, 200")

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(DPDK REQUIRED libdpdk)

    add_library(DPDK::SDK INTERFACE IMPORTED)
    target_include_directories(DPDK::SDK INTERFACE ${DPDK_INCLUDE_DIRS})
    target_link_directories(DPDK::SDK INTERFACE ${DPDK_LIBRARY_DIRS})

    # 🌟 DPDK 核武器绑定：强制打包所有驱动，防止网卡失联
    target_link_libraries(DPDK::SDK INTERFACE
            -Wl,--whole-archive ${DPDK_LIBRARIES} -Wl,--no-whole-archive
            pthread numa dl m
    )
    add_compile_definitions(ALLOW_EXPERIMENTAL_API)

    set(DRIVER_TARGET DPDK::SDK)
    set(DRIVER_SUBDIR "src/drivers/dpdk")

elseif(XNET_ENV STREQUAL "debian")
    # --- Linux TAP 配置 ---
    set(ENV_LOCAL_IP "192, 168, 66, 201")

    # TAP 驱动不需要外挂第三方库，直接使用 Linux 内核 API
    set(DRIVER_TARGET "")
    set(DRIVER_SUBDIR "src/drivers/tap")

else()
    message(FATAL_ERROR "不支持的环境变量 XNET_ENV=${XNET_ENV}，请使用 windows, ubuntu 或 debian")
endif()

# 🌟 全局注入本地 IP 宏，代码里直接用 XNET_CFG_LOCAL_IP 读取
add_compile_definitions(XNET_CFG_LOCAL_IP=${ENV_LOCAL_IP})

# =======================================================
# 3. 注册协议栈核心模块
# =======================================================
add_subdirectory(${DRIVER_SUBDIR}) # 根据环境动态加载对应的底层驱动目录
add_subdirectory(src/xnet_tiny)
add_subdirectory(src/xnet_socket)
add_subdirectory(src/xnet_app)

# =======================================================
# 🌟 4. 核心魔法：为 JNA 专用的动态库注入灵魂 (.so / .dll)
# =======================================================
if(TARGET xnet_socket)
    # 统一绑定：自动把当前环境需要的第三方库揉进去
    target_link_libraries(xnet_socket PRIVATE xnet_tiny xnet_netif ${DRIVER_TARGET})
endif()

# =======================================================
# 5. 生成主测试程序 (splendid_net.exe / splendid_net)
# =======================================================
add_executable(${PROJECT_NAME}
        src/app.c
        src/xnet_socket/xsocket.c
)

target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/xnet_tiny/core
        ${CMAKE_CURRENT_SOURCE_DIR}/src/xnet_tiny/transport
        ${CMAKE_CURRENT_SOURCE_DIR}/src/xnet_socket
        ${CMAKE_CURRENT_SOURCE_DIR}/src/xnet_app
        ${CMAKE_CURRENT_SOURCE_DIR}/src/xnet_tiny/app
)

# 统一绑定：无需再写 if/else，自动链接对应环境的驱动库
target_link_libraries(${PROJECT_NAME} PRIVATE
        xnet_app xnet_tiny xnet_netif ${DRIVER_TARGET}
)