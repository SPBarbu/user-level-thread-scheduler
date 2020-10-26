#include "sut.h"
#include <stdio.h>
#include <unistd.h>

void hello1() {
    for (int i = 0; i < 5; i++) {
        printf("%d Hello from hello1\n", i);
        usleep(1000 * 1000);
        sut_yield();
    }
    sut_exit();
}
void hello2() {
    for (int i = 0; i < 5; i++) {
        printf("%d Hello from hello2\n", i);
        usleep(1000 * 1000);
        sut_yield();
    }
    sut_exit();
}

void hello3() {
    for (int i = 0; i < 5; i++) {
        printf("%d Hello from hello3\n", i);
        usleep(1000 * 1000);
        sut_yield();
    }
    sut_exit();
}

int main() {
    sut_init();
    sut_create(hello1);
    sut_create(hello2);
    sut_create(hello3);
    sut_shutdown();
}