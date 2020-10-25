#include "sut.h"
#include <stdio.h>
#include <unistd.h>

int main() {
    sut_init();
    while (true) {
        printf("loop");
        usleep(1000 * 1000);
    }
}