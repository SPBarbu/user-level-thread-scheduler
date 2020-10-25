#include "sut.h"
#include <unistd.h>
#include <stdio.h>
#include <sys/prctl.h>

void* thread_CEXEC(void* args);
void* thread_IEXEC(void* args);

void sut_init() {
    pthread_t CEXEC_handle, IEXEC_handle;

    pthread_create(&CEXEC_handle, NULL, thread_CEXEC, NULL);
    pthread_create(&IEXEC_handle, NULL, thread_IEXEC, NULL);
}
bool sut_create(sut_task_f fn) {
    return false;
}
void sut_yield() {

}
void sut_exit() {

}
void sut_open(char* dest, int port) {

}
void sut_write(char* buf, int size) {

}
void sut_close() {

}
char* sut_read() {
    return 0;
}
void sut_shutdown() {

}

void* thread_CEXEC(void* args) {//main of the C-Exec
    while (true) {
        usleep(1000 * 1000);
        printf("Hello from CEXEC\n");
    }
    return NULL;
}

void* thread_IEXEC(void* args) {//main of the C-Exec
    while (true) {
        usleep(1000 * 1000);
        printf("Hello from IEXEC\n");
    }
    return NULL;
}