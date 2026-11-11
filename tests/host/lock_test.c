#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include "../../kernel/include/lock.h"

#define THREADS 4
#define ITERATIONS 100000
static struct nyr_spinlock lock = NYR_SPINLOCK_INITIALIZER;
static uint64_t counter;

static void *worker(void *unused) {
    (void)unused;
    for (unsigned int i = 0; i < ITERATIONS; ++i) {
        nyr_spin_lock(&lock);
        ++counter;
        nyr_spin_unlock(&lock);
    }
    return 0;
}
int main(void) {
    pthread_t threads[THREADS];
    for (unsigned int i = 0; i < THREADS; ++i)
        if (pthread_create(&threads[i], 0, worker, 0) != 0) return 1;
    for (unsigned int i = 0; i < THREADS; ++i)
        if (pthread_join(threads[i], 0) != 0) return 1;
    if (counter != (uint64_t)THREADS * ITERATIONS) {
        fprintf(stderr, "host lock test: expected %u, got %llu\n",
                THREADS * ITERATIONS, (unsigned long long)counter);
        return 1;
    }
    puts("host lock test: contended multi-threaded spinlock passed");
    return 0;
}
