file(GLOB TINY_SRCS CONFIGURE_DEPENDS "*.c")

add_library(xnet_tiny OBJECT ${TINY_SRCS})

target_include_directories(xnet_tiny
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        PRIVATE
        # 协议栈底层发包，需要调用 Port 层的接口
        ${PROJECT_SOURCE_DIR}/src/xnet_port
)