#include <stdio.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>

void* thread_1(void* args) {
    pthread_mutex_t* m = args;
    while (true) {
        pthread_mutex_lock(m);
        printf("1 Hello from\n");
        usleep(1000);
        printf("1 thread_1\n");
        pthread_mutex_unlock(m);
        usleep(1000 * 1000);
    }
}

void* thread_2(void* args) {
    pthread_mutex_t* m = args;
    while (true) {
        pthread_mutex_lock(m);
        printf("2 Hello from\n");
        usleep(1000);
        printf("2 thread_2\n");
        pthread_mutex_unlock(m);
        usleep(1000 * 1000);
    }
}

int main() {
    pthread_t thread_handle;
    pthread_t thread2_handle;

    pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;

    pthread_create(&thread_handle, NULL, thread_1, &m);
    pthread_create(&thread2_handle, NULL, thread_2, &m);

    pthread_join(thread_handle, NULL);
    pthread_join(thread2_handle, NULL);
}