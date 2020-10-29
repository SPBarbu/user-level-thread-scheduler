#ifndef __SUT_H__
#define __SUT_H__
#define _GNU_SOURCE

#include <pthread.h>
#include <stdbool.h>
#include <ucontext.h>

#define MAX_THREADS 32
#define THREAD_STACK_SIZE 1024*64
#define SOCKET_READ_SIZE 128

typedef void (*sut_task_f)();

typedef struct __tcb {
    ucontext_t* context;
    int sockfd; // assumes tasks can only have one io source/destination at a time
} tcb;

typedef enum __rType { _open, _close, _write, _read } _rType;

typedef struct __IOMessage {
    int sockfd;
    _rType rType;
    //union as to not waste as much memory since __IOMessage is the goto structure
    //for all IEXEC-CEXEC communication. Simplifies communication but wastes some memory
    union __request {
        struct __remote {
            char* dest;
            int port;
        }Remote;
        struct __message {
            char* message;
            int size;
        }Message;
    }Request;
}IOMessage;

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
