#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <time.h>
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
static uint64_t monotonic_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

int main(void) {
    const uint64_t start = monotonic_ns();
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
    const uint64_t elapsed = monotonic_ns() - start;
    const uint64_t operations = (uint64_t)THREADS * ITERATIONS;
    printf("host lock benchmark: threads=%u operations=%llu ns_per_operation=%llu\n",
           THREADS, (unsigned long long)operations,
           (unsigned long long)(elapsed / operations));
    puts("host lock test: contended multi-threaded spinlock passed");
    return 0;
}
