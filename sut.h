#ifndef __SUT_H__
#define __SUT_H__
#define _GNU_SOURCE

#include <pthread.h>
#include <stdbool.h>
#include <ucontext.h>

#define MAX_THREADS 16
#define THREAD_STACK_SIZE 1024*64

typedef void (*sut_task_f)();

typedef struct __tcb {
    ucontext_t* context;
    int sockfd; // assumes tasks can only have one io source/destination at a time
} tcb;

typedef struct __IOmessage {
    bool validMessage;//indicates if the message can be consummed, or is deprecated from last consume
    int sockfd;
    enum __rType { _open, _close, _message } rType;
    union __request {
        struct __remote {
            char* dest;
            int port;
        }remote;
        struct __message {
            char message[128];
            int size;
        }message;
    }request;
}IOmessage;

void sut_init();
bool sut_create(sut_task_f fn);
void sut_yield();
void sut_exit();
void sut_open(char* dest, int port);
void sut_write(char* buf, int size);
void sut_close();
char* sut_read();
void sut_shutdown();

#endif
