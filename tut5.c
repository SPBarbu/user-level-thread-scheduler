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

// void test_uc_link_behavior() {
//     printf("returning\n");
// }

int main() {

    ucontext_t t1, t2, m; //t3
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

    // getcontext(&t3);
    // t3.uc_stack.ss_sp = f1stack;
    // t3.uc_stack.ss_size = sizeof(f1stack);
    // t3.uc_link = &m;
    // makecontext(&t3, (void(*)(void))test_uc_link_behavior, 0);
    // swapcontext(&m, &t3);
    // printf("returned\n");
    // swapcontext(&m, &t3);

    /**
     * Prints, returning\n returned\n returning\n
     * Indicating that swapping context with a terminated task
     * makes it restart from the beginning
     */

    while (true) {
        swapcontext(&m, &t1);//swap our context to t1 context. m is initialized and swapped
        swapcontext(&m, &t2);
    }

    printf("exit from main\n");
}