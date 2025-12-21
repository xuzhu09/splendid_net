set(APP_SRCS
        xserver_datetime.c
        xserver_http.c
)

add_library(xnet_app OBJECT ${APP_SRCS})

target_include_directories(xnet_app
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        PRIVATE
        # 应用层只需要调用协议栈的核心接口
        ${PROJECT_SOURCE_DIR}/src/xnet_tiny
)