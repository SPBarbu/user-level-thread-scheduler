#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "sut.h"
#include "queue.h"

void* thread_CEXEC(void* args);
void* thread_IEXEC(void* args);
void IEXEC_open();
void IEXEC_close();
void IEXEC_write();
void IEXEC_read();
int connect_to_server(const char* host, uint16_t port, int* sockfd);

pthread_t CEXEC_handle, IEXEC_handle;
ucontext_t CEXEC_context;
int numThreads;
bool shutdown_;
pthread_mutex_t mxNumThreads = PTHREAD_MUTEX_INITIALIZER;//prevent data race on create same time as exit

//CEXEC global vars
struct queue_entry* contextToCleanOnExit;//called from inside task so no locking
pthread_mutex_t mxQReadyThreads = PTHREAD_MUTEX_INITIALIZER;
struct queue qReadyThreads;//FIFO queue
bool toWaitListSelfContext = false;

//IEXEC global vars
struct queue qWaitingTasks;
struct queue qtoioMessages;
//need one mx for both queues so iexec doesnt start consuming one while the other is still unfinished
pthread_mutex_t mxQWaitingIO = PTHREAD_MUTEX_INITIALIZER;

char fromIOMessage[SOCKET_READ_SIZE];
pthread_mutex_t mxFromIO = PTHREAD_MUTEX_INITIALIZER;
char readReturnMessage[SOCKET_READ_SIZE];

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

    //build io messages as packet
    IOMessage* ioMessage = (IOMessage*)malloc(sizeof(IOMessage));
    ioMessage->rType = _open;
    ioMessage->Request.Remote.dest = dest;
    ioMessage->Request.Remote.port = port;


    struct queue_entry* ioNode = queue_new_node(ioMessage);

    //indicate to CEXEC to put self on waiting q
    toWaitListSelfContext = true;

    pthread_mutex_lock(&mxQWaitingIO);

    queue_insert_tail(&qtoioMessages, ioNode);

    sut_yield();
    //expect CEXEC to put our context on the waiting queue
    //& unlock both the waiting queue and ioMessages queue
    //so can preserve adjancency of task in waitq and iomessage
    //we can resume from immediately after the context swap
}

void sut_write(char* buf, int size) {
    //build io messages as packet
    struct queue_entry* nodeSelf = queue_peek_front(&qReadyThreads);

    IOMessage* ioMessage = (IOMessage*)malloc(sizeof(IOMessage));
    ioMessage->sockfd = ((tcb*)nodeSelf->data)->sockfd;
    ioMessage->rType = _write;
    ioMessage->Request.Message.message = buf;
    ioMessage->Request.Message.size = size;

    struct queue_entry* ioNode = queue_new_node(ioMessage);

    pthread_mutex_lock(&mxQWaitingIO);
    queue_insert_tail(&qtoioMessages, ioNode);
    pthread_mutex_unlock(&mxQWaitingIO);

}

void sut_close() {
    //build io messages as packet
    struct queue_entry* nodeSelf = queue_peek_front(&qReadyThreads);

    IOMessage* ioMessage = (IOMessage*)malloc(sizeof(IOMessage));
    ioMessage->sockfd = ((tcb*)nodeSelf->data)->sockfd;
    ioMessage->rType = _close;

    struct queue_entry* ioNode = queue_new_node(ioMessage);

    pthread_mutex_lock(&mxQWaitingIO);
    queue_insert_tail(&qtoioMessages, ioNode);
    pthread_mutex_unlock(&mxQWaitingIO);

}

//TCP connection, so caller(library user) is expected to verify if the data is complete
//or call sut_read until data is fully received
char* sut_read() {
    //build io messages as packet
    IOMessage* ioMessage = (IOMessage*)malloc(sizeof(IOMessage));
    ioMessage->rType = _read;
    //dont need to add sockfd. will get from tcb in IEXEC

    struct queue_entry* ioNode = queue_new_node(ioMessage);

    //indicate to CEXEC to put self on waiting q
    toWaitListSelfContext = true;

    pthread_mutex_lock(&mxQWaitingIO);

    queue_insert_tail(&qtoioMessages, ioNode);

    sut_yield();
    //expect CEXEC to put our context on the waiting queue
    //& unlock both the waiting queue and ioMessages queue
    //so can preserve adjancency of task in waitq and iomessage
    //we can resume from immediately after the context swap

    //resume here from context swap

    //IEXEC expects sut_read to unlock the mutex
    //ensures blocking until the message has been consummed. Cant be overwritten
    strncpy(readReturnMessage, fromIOMessage, SOCKET_READ_SIZE);
    pthread_mutex_unlock(&mxFromIO);

    //we know that if IEXEC reads another message it'll overwrite fromIOMessage
    //but readReturnMessage is overwritten only once the previous task has complete
    //and we are in another instruction so readReturnMessage has been consummed
    //this way dont need to trust user to free memory.
    return readReturnMessage;
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

                pthread_mutex_unlock(&mxQWaitingIO);//allows IEXEC to start working on waiting tasks
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
            //busy waiting while there are no IEXEC tasks
            //possibly solve by having semaphores signal when task has been queued
            usleep(100);//100us
        }
    }
    return 0;
}

void* thread_IEXEC(void* args) {//main of the C-Exec
    struct queue_entry* ioMessageNode = queue_peek_front(&qtoioMessages);
    struct queue_entry* asynIONode;
    _rType requestType;
    while (!shutdown_ || numThreads || ioMessageNode) {
        if (ioMessageNode) {
            requestType = ((IOMessage*)ioMessageNode->data)->rType;
            if (requestType == _open)
                IEXEC_open();
            else if (requestType == _close)
                IEXEC_close();
            else if (requestType == _write)
                IEXEC_write();
            else if (requestType == _read)
                IEXEC_read();
        }
        else {
            usleep(100);//100us
        }

        //update if tasks are to be processed
        ioMessageNode = queue_peek_front(&qtoioMessages);
    }
    return 0;
}

void IEXEC_open() {
    //Open a socket to the server specified in iomessage
    //and save it in the tcb of the calling task
    //which is expected to be at the head of the waiting queue

    pthread_mutex_lock(&mxQWaitingIO);
    struct queue_entry* selfWaitNode = queue_pop_head(&qWaitingTasks);
    struct queue_entry* selfIOMessageNode = queue_pop_head(&qtoioMessages);
    pthread_mutex_unlock(&mxQWaitingIO);

    char* dest = ((IOMessage*)selfIOMessageNode->data)->Request.Remote.dest;
    int port = ((IOMessage*)selfIOMessageNode->data)->Request.Remote.port;
    int* sockfd = &(((tcb*)selfWaitNode->data)->sockfd);

    connect_to_server(dest, (uint16_t)port, sockfd);

    //reinsert waiting context in readyqueue
    pthread_mutex_lock(&mxQReadyThreads);
    queue_insert_tail(&qReadyThreads, selfWaitNode);
    pthread_mutex_unlock(&mxQReadyThreads);

    free((IOMessage*)selfIOMessageNode->data);
    free(selfIOMessageNode);

}

void IEXEC_write() {
    //assumes a socket exists
    //sends the message stored in iomessage through it

    pthread_mutex_lock(&mxQWaitingIO);
    struct queue_entry* selfIOMessageNode = queue_pop_head(&qtoioMessages);
    pthread_mutex_unlock(&mxQWaitingIO);

    char* buf = ((IOMessage*)selfIOMessageNode->data)->Request.Message.message;
    int size = ((IOMessage*)selfIOMessageNode->data)->Request.Message.size;
    int sockfd = ((IOMessage*)selfIOMessageNode->data)->sockfd;

    send(sockfd, buf, (size_t)size, 0);

    free((IOMessage*)selfIOMessageNode->data);
    free(selfIOMessageNode);
}

void IEXEC_close() {
    //close the file descriptor associated with the socket in the context's tcb
    pthread_mutex_lock(&mxQWaitingIO);
    struct queue_entry* selfIOMessageNode = queue_pop_head(&qtoioMessages);
    pthread_mutex_unlock(&mxQWaitingIO);

    int sockfd = ((IOMessage*)selfIOMessageNode->data)->sockfd;

    close(sockfd);

    free((IOMessage*)selfIOMessageNode->data);
    free(selfIOMessageNode);
}


void IEXEC_read() {
    //received a fixed amount of bytes from the socket
    //save them to fromIOmessage to be read by the returning sut_read
    pthread_mutex_lock(&mxQWaitingIO);
    struct queue_entry* selfWaitNode = queue_pop_head(&qWaitingTasks);
    struct queue_entry* selfIOMessageNode = queue_pop_head(&qtoioMessages);
    pthread_mutex_unlock(&mxQWaitingIO);

    int sockfd = ((tcb*)selfWaitNode->data)->sockfd;

    //expects returning sut_read to unlock the mutex
    //prevents other read requests from overridding fromIOmessage until sut_read has consummed it
    pthread_mutex_lock(&mxFromIO);
    memset(fromIOMessage, 0, sizeof(fromIOMessage));

    //TCP connection, so caller(library user) is expected to verify if the data is complete
    //or call sut_read until data is fully received
    recv(sockfd, fromIOMessage, (size_t)128, 0);

    pthread_mutex_lock(&mxQReadyThreads);
    queue_insert_tail(&qReadyThreads, selfWaitNode);
    pthread_mutex_unlock(&mxQReadyThreads);

    free((IOMessage*)selfIOMessageNode->data);
    free(selfIOMessageNode);
}

int connect_to_server(const char* host, uint16_t port, int* sockfd) {
    struct sockaddr_in server_address = { 0 };

    // create a new socket
    *sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (*sockfd < 0) {
        perror("Failed to create a new socket\n");
        return -1;
    }

    // connect to server
    server_address.sin_family = AF_INET;
    inet_pton(AF_INET, host, &(server_address.sin_addr.s_addr));
    server_address.sin_port = htons(port);
    if (connect(*sockfd, (struct sockaddr*)&server_address, sizeof(server_address)) < 0) {
        perror("Failed to connect to server\n");
        return -1;
    }
    return 0;
}