#include "sut.h"
#include <stdio.h>
#include <unistd.h>

void hello1() {
    while (true) {
        printf("Hello from hello1");
    }
}

int main() {
    sut_init();
    sut_create(hello1);
}