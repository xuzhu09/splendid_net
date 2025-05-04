#include <stdio.h>

int main (void) {
    setvbuf(stdout, NULL, _IONBF, 0);  // 禁用缓冲，保证 printf 立刻显示

    printf("xnet running\n");

    while (1) {
    }

    return 0;
}