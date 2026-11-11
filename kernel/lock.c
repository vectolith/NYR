#include <stdbool.h>
#include "include/lock.h"

void nyr_spin_lock(struct nyr_spinlock *lock) {
    for (;;) {
        uint32_t expected = 0;
        if (__atomic_compare_exchange_n(&lock->held, &expected, 1u, false,
                                        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
            return;
        }
        while (__atomic_load_n(&lock->held, __ATOMIC_RELAXED) != 0u) {
            __asm__ volatile ("pause" ::: "memory");
        }
    }
}

void nyr_spin_unlock(struct nyr_spinlock *lock) {
    __atomic_store_n(&lock->held, 0u, __ATOMIC_RELEASE);
}

uint64_t nyr_spin_lock_irqsave(struct nyr_spinlock *lock) {
#ifdef NYR_HOST_TEST
    nyr_spin_lock(lock);
    return 0;
#else
    uint64_t flags;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    nyr_spin_lock(lock);
    return flags;
#endif
}

void nyr_spin_unlock_irqrestore(struct nyr_spinlock *lock, uint64_t flags) {
    nyr_spin_unlock(lock);
#ifndef NYR_HOST_TEST
    __asm__ volatile ("pushq %0; popfq" : : "r"(flags) : "memory", "cc");
#else
    (void)flags;
#endif
}
