#include "sut.h"
#include <stdio.h>
#include <unistd.h>

void hello1() {
    while (true) {
        printf("Hello from hello1\n");
        usleep(1000 * 1000);
        sut_yield();
    }
}
void hello2() {
    while (true) {
        printf("Hello from hello2\n");
        usleep(1000 * 1000);
        sut_yield();
    }
}

void hello3() {
    while (true) {
        printf("Hello from hello3\n");
        usleep(1000 * 1000);
        sut_yield();
    }
}

int main() {
    sut_init();
    sut_create(hello1);
    sut_create(hello2);
    sut_create(hello3);
    while (true) {
        usleep(1000 * 1000);
    }
}