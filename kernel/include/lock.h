#ifndef NYR_LOCK_H
#define NYR_LOCK_H

#include <stdint.h>

struct nyr_spinlock {
    uint32_t held;
};

#define NYR_SPINLOCK_INITIALIZER { 0u }

void nyr_spin_lock(struct nyr_spinlock *lock);
void nyr_spin_unlock(struct nyr_spinlock *lock);
uint64_t nyr_spin_lock_irqsave(struct nyr_spinlock *lock);
void nyr_spin_unlock_irqrestore(struct nyr_spinlock *lock, uint64_t flags);

#endif
