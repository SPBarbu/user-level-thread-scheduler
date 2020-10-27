#include "sut.h"
#include <unistd.h>
#include <stdio.h>
#include "queue.h"
#include "io.h"

void* thread_CEXEC(void* args);
void* thread_IEXEC(void* args);
void IEXEC_open();

pthread_t CEXEC_handle, IEXEC_handle;
ucontext_t CEXEC_context;
struct queue_entry* contextToCleanOnExit;
struct queue_entry* contextWaitingForIO;//assumes only one waiter at a time ie. if two, last will overwrite first
int numThreads;
bool shutdown_;
pthread_mutex_t mxNumThreads = PTHREAD_MUTEX_INITIALIZER;//prevent data race on create same time as exit
pthread_mutex_t mxQReadyThreads = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mxContextWaitingForIO_mxIOMessage = PTHREAD_MUTEX_INITIALIZER;//shared between CEXEC & IEXEC
struct queue qReadyThreads;//FIFO queue
IOmessage ioMessage;

void sut_init() {
    numThreads = 0;
    shutdown_ = false;

    qReadyThreads = queue_create();
    queue_init(&qReadyThreads);//never move, only refer through pointer indirect

    struct queue_entry* node = queue_pop_head(&qReadyThreads);

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

    //make tasks' context
    tcb* tTcb = (tcb*)malloc(sizeof(tcb));
    ucontext_t* tContext = (ucontext_t*)malloc(sizeof(ucontext_t));
    void* tStack = (void*)malloc(THREAD_STACK_SIZE);

    getcontext(tContext);
    tContext->uc_stack.ss_sp = tStack;//stack pointer
    tContext->uc_stack.ss_size = THREAD_STACK_SIZE;//stack size
    tContext->uc_stack.ss_flags = 0;
    tContext->uc_link = &CEXEC_context;//return to on task terminate
        //error state, shouldnt be allowed to terminate on its own; call sut_exit()
    makecontext(tContext, fn, 0);
    tTcb->context = tContext;

    struct queue_entry* node = queue_new_node(tTcb);
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
    swapcontext((ucontext_t*)((tcb*)selfNode->data)->context, &CEXEC_context);
}

void sut_exit() {
    pthread_mutex_lock(&mxNumThreads);
    numThreads--;
    pthread_mutex_unlock(&mxNumThreads);

    pthread_mutex_lock(&mxQReadyThreads);//prevents data race on q when sut_exit & sut_create
    contextToCleanOnExit = queue_pop_head(&qReadyThreads);//extract & never put back
    pthread_mutex_unlock(&mxQReadyThreads);
    swapcontext((ucontext_t*)((tcb*)contextToCleanOnExit->data)->context, &CEXEC_context);
}

void sut_open(char* dest, int port) {
    struct queue_entry* nodeSelf;
    //sut_open blocking so pop from ready queue
    pthread_mutex_lock(&mxQReadyThreads);
    nodeSelf = queue_pop_head(&qReadyThreads);//first is self
    pthread_mutex_unlock(&mxQReadyThreads);

    pthread_mutex_lock(&mxContextWaitingForIO_mxIOMessage);
    contextWaitingForIO = nodeSelf;//place self on IO waiting

    //build IO message
    ioMessage.sockfd = ((tcb*)nodeSelf->data)->sockfd;
    ioMessage.rType = _open;
    ioMessage.request.remote.dest = dest;
    ioMessage.request.remote.port = port;

    //expect CEXEC to unlock waiting&iomessage once context swapped
    //no unlock = sut_open is blocking and returns after swap context
    //dont want to unlock because swapcontext will write to contextWaitingForIO which is shared with IEXEC

    swapcontext(((tcb*)contextWaitingForIO->data)->context, &CEXEC_context);

}

void sut_write(char* buf, int size) {

}

void sut_close() {

}

char* sut_read() {
    return 0;
}

void sut_shutdown() {
    shutdown_ = true;//no lock b/c only writer

    pthread_join(CEXEC_handle, NULL);
    pthread_join(IEXEC_handle, NULL);
}

void* thread_CEXEC(void* args) {//main of the C-Exec
    struct queue_entry* node;
    while (!shutdown_ || numThreads) {
        node = queue_peek_front(&qReadyThreads);
        if (node) {//if there are tasks queued

            swapcontext(&CEXEC_context, (ucontext_t*)((tcb*)node->data)->context);

            //once come back from context swap
            if (contextToCleanOnExit) {//if scheduled to clean means sut_exit called
               //free allocated mem to context
                free((ucontext_t*)(((tcb*)contextToCleanOnExit->data)->context)->uc_stack.ss_sp);
                free((ucontext_t*)((tcb*)contextToCleanOnExit->data)->context);
                free((tcb*)contextToCleanOnExit->data);
                free(contextToCleanOnExit);//mem for queue node
                contextToCleanOnExit = 0;
            }
            else if (contextWaitingForIO) {
                pthread_mutex_unlock(&mxContextWaitingForIO_mxIOMessage);//allows IEXEC to start working on waiters
            }
            else {//sut_exit not called, requeue task
                pthread_mutex_lock(&mxQReadyThreads);
                node = queue_pop_head(&qReadyThreads);//extract node
                        //should be last task executed since CEXEC only manipulator of head of queue
                if (node) {//if there remains tasks in queue. b/c task may have called sut_exit while being the last task in queue
                    queue_insert_tail(&qReadyThreads, node);//reinsert last
                }
                pthread_mutex_unlock(&mxQReadyThreads);
            }

        }
        else {
            usleep(100);//100us
        }
    }
    return NULL;
}

void* thread_IEXEC(void* args) {//main of the C-Exec
    while (!shutdown_ || numThreads) {
        usleep(1000 * 1000);
        printf("Hello from IEXEC\n");
    }
    return NULL;
}

void IEXEC_open() {

}