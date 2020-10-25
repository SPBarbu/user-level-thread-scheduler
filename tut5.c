#include <stdlib.h>
#include <stdio.h>
#include <ucontext.h>
#include <unistd.h>
#include <stdbool.h>

void f1(void* main, void* self) {
    int numIter = 0;
    while (true) {
        printf("Hello from thread 1 called %d\n", numIter);
        numIter++;
        usleep(1000 * 1000);
        swapcontext(self, main);
    }
}

void f2(void* main, void* self) {
    while (true) {
        printf("Hello from thread 2\n");
        usleep(1000 * 1000);
        swapcontext(self, main);
    }
}

int main() {

    ucontext_t t1, t2, m;
    char f1stack[16 * 1024];
    char f2stack[16 * 1024];

    getcontext(&t1); //t1 has context of main
    t1.uc_stack.ss_sp = f1stack; //stack pointer
    t1.uc_stack.ss_size = sizeof(f1stack); //sizeof stack
    t1.uc_link = &m; //where to return to
    makecontext(&t1, (void(*)(void))f1, 2, &m, &t1);//initializes t1 as context and initial state

    getcontext(&t2);
    t2.uc_stack.ss_sp = f2stack;
    t2.uc_stack.ss_size = sizeof(f2stack);
    t2.uc_link = &m;
    makecontext(&t2, (void(*)(void))f2, 2, &m, &t2);//cast to accomodate makecontext signature

    while (true) {
        swapcontext(&m, &t1);//swap our context to t1 context. m is initialized and swapped
        swapcontext(&m, &t2);
    }

    printf("exit from main\n");
}