#include "sut.h"
#include <unistd.h>
#include <stdio.h>

void hello1() {
    int i;
    sut_open("0.0.0.0", 10000);
    for (i = 0; i < 1; i++) {
        // printf("Hello world!, this is SUT-One \n");
        sut_yield();
        sut_write("aaaa\n", 5);
        printf("%s\n", sut_read());
    }
    sut_close();
    usleep(1000000);
    sut_exit();
}

int main() {
    sut_init();
    sut_create(hello1);
    sut_shutdown();
}
