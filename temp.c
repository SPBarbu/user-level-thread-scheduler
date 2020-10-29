
/**
 * Assumes sut_open not called more than once before calling close.
 * If it is, previous handle is overwritten and is lost.
 */
void sut_open(char* dest, int port) {
    struct queue_entry* nodeSelf;
    //sut_open blocking so pop from ready queue
    pthread_mutex_lock(&mxQReadyThreads);
    nodeSelf = queue_pop_head(&qReadyThreads);//first is self
    pthread_mutex_unlock(&mxQReadyThreads);

    pthread_mutex_lock(&mxContextWaitingForIO_mxIOMessage);
    contextWaitingForIO = nodeSelf;//place self on IO waiting

    //build IO message
    ioMessage.rType = _open;
    ioMessage.request.remote.dest = dest;
    ioMessage.request.remote.port = port;
    ioMessage.validMessage = true;

    //expect CEXEC to unlock waiting&iomessage once context swapped
    //no unlock = sut_open is blocking and returns after swap context
    //dont want to unlock because swapcontext will write to contextWaitingForIO which is shared with IEXEC

    swapcontext(((tcb*)contextWaitingForIO->data)->context, &CEXEC_context);

}

void sut_write(char* buf, int size) {
    char* bufcpy = strndup(buf, size);
    struct queue_entry* nodeSelf = queue_peek_front(&qReadyThreads);

    pthread_mutex_lock(&mxContextWaitingForIO_mxIOMessage);
    ioMessage.sockfd = ((tcb*)nodeSelf->data)->sockfd;
    ioMessage.rType = _write;
    ioMessage.request.message.message = bufcpy;
    ioMessage.request.message.size = size;
    //have to indicate validMessage last, otherwise IEXEC will start consuming
    ioMessage.validMessage = true;

    //no unlock expect IEXEC to unlock. blocks other threads from overwriting once context swap
}

void sut_close() {
    struct queue_entry* nodeSelf = queue_peek_front(&qReadyThreads);

    pthread_mutex_lock(&mxContextWaitingForIO_mxIOMessage);
    //build IO message
    ioMessage.sockfd = ((tcb*)nodeSelf->data)->sockfd;
    ioMessage.rType = _close;
    //have to indicate validMessage last, otherwise IEXEC can/will start consuming
    ioMessage.validMessage = true;

    //no unlock expect IEXEC to unlock. blocks other threads from overwriting once context swap
}

char* sut_read() {
    struct queue_entry* nodeSelf;
    //sut_open blocking so pop from ready queue
    pthread_mutex_lock(&mxQReadyThreads);
    nodeSelf = queue_pop_head(&qReadyThreads);//first is self
    pthread_mutex_unlock(&mxQReadyThreads);

    pthread_mutex_lock(&mxContextWaitingForIO_mxIOMessage);
    contextWaitingForIO = nodeSelf;//place self on IO waiting

    //build IO message
    ioMessage.rType = _read;
    ioMessage.sockfd = ((tcb*)nodeSelf->data)->sockfd;
    ioMessage.validMessage = true;

    //expect CEXEC to unlock waiting&iomessage once context swapped
    //no unlock = sut_read is blocking and returns after swap context
    //dont want to unlock because swapcontext will write to contextWaitingForIO which is shared with IEXEC

    swapcontext(((tcb*)contextWaitingForIO->data)->context, &CEXEC_context);

    //once request has been processed, message put back in ready queue
    //sut_read resumes here, but ioMessage contains received message & is locked. expected to unlock

    char* retMessage = ioMessage.request.message.message;
    ioMessage.validMessage = false;
    pthread_mutex_unlock(&mxContextWaitingForIO_mxIOMessage);

    return retMessage;

}

void IEXEC_read() {
    pthread_mutex_lock(&mxContextWaitingForIO_mxIOMessage);
    recv_message(ioMessage.sockfd, ioMessage.request.message.message, (size_t)SOCKET_READ_SIZE);

    pthread_mutex_lock(&mxQReadyThreads);
    queue_insert_tail(&qReadyThreads, contextWaitingForIO);//reinsert waiting context in readyqueue
    contextWaitingForIO = 0;//remove waiting context from waitings
    pthread_mutex_unlock(&mxQReadyThreads);

    //ioMessage.validMessage still true
    //expect sut_read to unlock waiting q & iomessage
}

void IEXEC_write() {
    //expected to unlock already locked ioMessage
    send_message(ioMessage.sockfd, ioMessage.request.message.message, (size_t)ioMessage.request.message.size);
    //discard output, irrelevant success status b/c async
    free(ioMessage.request.message.message);
    ioMessage.validMessage = false;
    pthread_mutex_unlock(&mxContextWaitingForIO_mxIOMessage);
}

void IEXEC_close() {
    //expected to unlock already locked ioMessage
    close(ioMessage.sockfd);//close file descriptor as requested
    ioMessage.validMessage = false;
    pthread_mutex_unlock(&mxContextWaitingForIO_mxIOMessage);
}

void IEXEC_open() {
    pthread_mutex_lock(&mxContextWaitingForIO_mxIOMessage);
    //connect to server and update the handle to it in the tcb of the waiting context
    connect_to_server(ioMessage.request.remote.dest, (uint16_t)ioMessage.request.remote.port, &(((tcb*)contextWaitingForIO->data)->sockfd));

    pthread_mutex_lock(&mxQReadyThreads);
    queue_insert_tail(&qReadyThreads, contextWaitingForIO); //reinsert waiting context in readyqueue
    contextWaitingForIO = 0;//remove waiting context from waiting
    ioMessage.validMessage = false;
    pthread_mutex_unlock(&mxQReadyThreads);

    pthread_mutex_unlock(&mxContextWaitingForIO_mxIOMessage);
}