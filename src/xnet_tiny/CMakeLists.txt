aux_source_directory(. DIR_HELLO_SRCS)
add_library(xnet_tiny  ${DIR_HELLO_SRCS}
        xnet_ethernet.c
        xnet_ethernet.h
        xnet_arp.h
        xnet_arp.c
        xnet_ip.h
        xnet_ip.c
        xnet_icmp.h
        xnet_icmp.c
        xnet_udp.h
        xnet_udp.c)
