cmake_minimum_required(VERSION 3.16)
project(splendid_net C)

set(CMAKE_C_STANDARD 99)

if(NOT WIN32)
    add_compile_options(-march=native)
endif()

# =======================================================
# 1. 定义外部依赖 (这是 xnet_port 能工作的关键！)
# =======================================================
if(WIN32)
    # --- Windows Npcap 配置 ---
    set(NPCAP_SDK_DIR "C:/Users/efair/Downloads/npcap-sdk-1.15")

    # 定义 Npcap::SDK 目标，供子模块引用
    add_library(Npcap::SDK UNKNOWN IMPORTED)
    set_target_properties(Npcap::SDK PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${NPCAP_SDK_DIR}/Include"
            IMPORTED_LOCATION "${NPCAP_SDK_DIR}/Lib/x64/wpcap.lib"
            INTERFACE_LINK_LIBRARIES "${NPCAP_SDK_DIR}/Lib/x64/Packet.lib;Ws2_32"
    )
else()
    # --- Linux DPDK 配置 ---
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(DPDK REQUIRED libdpdk)

    add_library(DPDK::SDK INTERFACE IMPORTED)
    target_include_directories(DPDK::SDK INTERFACE ${DPDK_INCLUDE_DIRS})
    target_link_directories(DPDK::SDK INTERFACE ${DPDK_LIBRARY_DIRS})
    target_link_libraries(DPDK::SDK INTERFACE
            -Wl,--whole-archive ${DPDK_LIBRARIES} -Wl,--no-whole-archive
            pthread numa dl m
    )
    add_compile_definitions(ALLOW_EXPERIMENTAL_API)
endif()

# =======================================================
# 2. 添加子模块 (顺序无所谓)
# =======================================================
add_subdirectory(src/xnet_tiny)
add_subdirectory(src/xnet_app)
add_subdirectory(src/xnet_port)

# =======================================================
# 3. 生成可执行文件
# =======================================================
add_executable(${PROJECT_NAME} src/app.c)

# =======================================================
# 4. 最终链接 (极简模式)
# =======================================================
# 因为用了 OBJECT 库，这里把所有模块串起来
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE
            xnet_app      # 业务层
            xnet_tiny     # 协议层
            xnet_port     # 驱动层
            Npcap::SDK    # 外部依赖
    )
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE
            xnet_app
            xnet_tiny
            xnet_port
            DPDK::SDK
    )
endif()