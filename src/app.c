#include <stdio.h>

#include "xnet_tiny.h"
#include "xserver_http.h"
#include "xserver_datetime.h"

int main (void) {
    setvbuf(stdout, NULL, _IONBF, 0);  // 禁用缓冲，保证 printf 立刻显示

    xnet_init();

    xserver_datetime_create(13);
    xserver_http_create(80);

    printf("xnet running......\n");
    while (1) {
        xnet_poll();
    }

    return 0;
}