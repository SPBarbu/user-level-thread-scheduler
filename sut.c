#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include "sut.h"
#include "queue.h"
#include "io.h"

#define SOCKET_READ_SIZE 128

void* thread_CEXEC(void* args);
void* thread_IEXEC(void* args);
void IEXEC_open();
void IEXEC_close();
void IEXEC_write();
void IEXEC_read();

pthread_t CEXEC_handle, IEXEC_handle;
ucontext_t CEXEC_context;
int numThreads;
bool shutdown_;
pthread_mutex_t mxNumThreads = PTHREAD_MUTEX_INITIALIZER;//prevent data race on create same time as exit

//CEXEC global vars
struct queue_entry* contextToCleanOnExit;
pthread_mutex_t mxQReadyThreads = PTHREAD_MUTEX_INITIALIZER;
struct queue qReadyThreads;//FIFO queue
bool toWaitListSelfContext = false;

//IEXEC global vars
struct queue qWaitingTasks;
struct queue qtoioMessages;
//need one mx for both queues so iexec doesnt start consuming one while the other is still unfinished
pthread_mutex_t mx_waiting_io_queues = PTHREAD_MUTEX_INITIALIZER;

void sut_init() {
    numThreads = 0;
    shutdown_ = false;

    qReadyThreads = queue_create();
    queue_init(&qReadyThreads);//never move, only refer through pointer indirect

    qWaitingTasks = queue_create();
    queue_init(&qWaitingTasks);

    qtoioMessages = queue_create();
    queue_init(&qtoioMessages);

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

    tTcb->sockfd = -1;//indicate no socket open

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

void sut_shutdown() {
    shutdown_ = true;//no lock b/c only writer

    pthread_join(CEXEC_handle, NULL);
    pthread_join(IEXEC_handle, NULL);
}

void sut_open(char* dest, int port) {

    //build iomessage
    IOMessage* ioMessage = (IOMessage*)malloc(sizeof(IOMessage)); //TOFREE
    ioMessage->rType = _open;
    ioMessage->Request.Remote.dest = dest;
    ioMessage->Request.Remote.port = port;


    struct queue_entry* ioNode = queue_new_node(ioMessage);//TOFREE

    //indicate to CEXEC to put self on waiting q
    toWaitListSelfContext = true;

    pthread_mutex_lock(&mx_waiting_io_queues);

    queue_insert_tail(&qtoioMessages, ioNode);

    sut_yield();
    //expect CEXEC to put our context on the waiting queue
    //& unlock both the waiting queue and ioMessages queue
}

void* thread_CEXEC(void* args) {//main of the C-Exec
    struct queue_entry* node;
    while (!shutdown_ || numThreads) {// || ioMessage.validMessage
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
            else if (toWaitListSelfContext) {
                node = queue_pop_head(&qReadyThreads);
                queue_insert_tail(&qWaitingTasks, node);

                pthread_mutex_unlock(&mx_waiting_io_queues);//allows IEXEC to start working on waiting tasks
                toWaitListSelfContext = false;//indicates task has been moved to wait list
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
    return 0;
}

void* thread_IEXEC(void* args) {//main of the C-Exec
    struct queue_entry* ioMessageNode = queue_peek_front(&qtoioMessages);
    _rType requestType;
    while (!shutdown_ || numThreads || ioMessageNode) {
        if (ioMessageNode) {
            requestType = ((IOMessage*)ioMessageNode->data)->rType;
            if (requestType == _open)
                IEXEC_open();
            // else if (requestType == _close)
            //     IEXEC_close();
            // else if (requestType == _write)
            //     IEXEC_write();
            // else if (requestType == _read)
            //     IEXEC_read();
        }
        else {
            usleep(100);
            // printf("Hello from IEXEC\n");
        }
        ioMessageNode = queue_peek_front(&qtoioMessages);
    }
    return 0;
}

void IEXEC_open() {
    //connect to server and update the handle to it in the tcb of the waiting context

    pthread_mutex_lock(&mx_waiting_io_queues);

    struct queue_entry* selfWaitNode = queue_pop_head(&qWaitingTasks);
    struct queue_entry* selfIOMessageNode = queue_pop_head(&qtoioMessages);

    pthread_mutex_unlock(&mx_waiting_io_queues);

    char* dest = ((IOMessage*)selfIOMessageNode->data)->Request.Remote.dest;
    int port = ((IOMessage*)selfIOMessageNode->data)->Request.Remote.port;

    printf("%d\n", connect_to_server(dest, (uint16_t)port, &((tcb*)selfWaitNode->data)->sockfd));

    pthread_mutex_lock(&mxQReadyThreads);
    queue_insert_tail(&qReadyThreads, selfWaitNode); //reinsert waiting context in readyqueue
    pthread_mutex_unlock(&mxQReadyThreads);

    free((IOMessage*)selfIOMessageNode->data);
    free(selfIOMessageNode);

}