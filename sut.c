#include "sut.h"
#include <unistd.h>
#include <stdio.h>
#include <sys/prctl.h>
#include "queue/queue.h"

void* thread_CEXEC(void* args);
void* thread_IEXEC(void* args);

pthread_t CEXEC_handle, IEXEC_handle;
ucontext_t CEXEC_context;
int numThreads;
pthread_mutex_t mxNumThreads = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mxQReadyThreads = PTHREAD_MUTEX_INITIALIZER;
struct queue qReadyThreads;//FIFO queue

void sut_init() {
    numThreads = 0;

    qReadyThreads = queue_create();
    queue_init(&qReadyThreads);//never move, only refer through pointer indirect

    pthread_create(&CEXEC_handle, NULL, thread_CEXEC, NULL);
    pthread_create(&IEXEC_handle, NULL, thread_IEXEC, NULL);
}
bool sut_create(sut_task_f fn) {

    pthread_mutex_lock(&mxNumThreads);//prevent data race on threads spawning threads
    if (numThreads >= MAX_THREADS) {
        pthread_mutex_unlock(&mxNumThreads);
        perror("Maximum thread limit reached. Creation failed!\n");
        return false;
    }
    else {
        numThreads++;
        pthread_mutex_unlock(&mxNumThreads);
    }

    //make fn's context
    ucontext_t* tContext = (ucontext_t*)malloc(sizeof(ucontext_t));
    void* tStack = (void*)malloc(THREAD_STACK_SIZE);

    getcontext(tContext);
    tContext->uc_stack.ss_sp = tStack;//stack pointer
    tContext->uc_stack.ss_size = THREAD_STACK_SIZE;//stack size
    tContext->uc_link = &CEXEC_context;//return to on task terminate
        //error state, shouldnt be allowed to terminate on its own; call sut_exit()
    makecontext(tContext, fn, 0);

    struct queue_entry* node = queue_new_node(tContext);
    //prevent data race on inserting node into shared queue
    pthread_mutex_lock(&mxQReadyThreads);
    queue_insert_tail(&qReadyThreads, node);//FIFO queue = insert tail
    pthread_mutex_unlock(&mxQReadyThreads);

    return true;
}
void sut_yield() {
    struct queue_entry* selfNode;

    selfNode = queue_peek_front(&qReadyThreads);//expect first node to be self if FIFO
        //no need lock b/c solo running on CEXEC_thread & dont expect to be swapped b/c cooperative threading
    swapcontext((ucontext_t*)(selfNode->data), &CEXEC_context);
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

    // for (int i = 0; i < 5; i++) {
    //     struct queue_entry* node = queue_new_node(&j);
    //     queue_insert_tail(&qReadyThreads, node);
    // }
    // int* p = (int*)0x7ffff5d57e68;
    // *p = 3;
    // struct queue_entry* ptr;
    // for (int i = 0; i < 5; i++) {
    //     ptr = queue_pop_head(&qReadyThreads);
    //     printf("cleaning %d\n", *(int*)ptr->data);
    //     free(ptr);
    // }
    struct queue_entry* node;

    while (true) {
        pthread_mutex_lock(&mxQReadyThreads);//prevent race condition on empty queues and thread_create

        if (queue_peek_front(&qReadyThreads)) {//non empty queue
            node = queue_pop_head(&qReadyThreads);
            queue_insert_tail(&qReadyThreads, node);//reinsert at back
            pthread_mutex_unlock(&mxQReadyThreads);

            swapcontext(&CEXEC_context, (ucontext_t*)(node->data));
        }
        else {
            pthread_mutex_unlock(&mxQReadyThreads);

            usleep(1000 * 10);//10ms
        }
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