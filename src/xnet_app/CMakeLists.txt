# 定义通用的源文件 (Windows/Linux 都要用的)
set(APP_SRCS
        xserver_datetime.c
        xserver_datetime.h
        xserver_http.c
        xserver_http.h
)

if(WIN32)
    # -------- Windows 专用 --------
    # 只加入 pcap 的适配文件
    list(APPEND APP_SRCS port_pcap.c)
else()
    # -------- Linux 专用 --------
    # 只加入 DPDK 的适配文件
    list(APPEND APP_SRCS port_dpdk.c)
endif()

# 生成库
add_library(xnet_app ${APP_SRCS})