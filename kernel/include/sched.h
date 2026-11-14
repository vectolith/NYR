#ifndef NYR_SCHED_H
#define NYR_SCHED_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    NYR_TASK_UNUSED = 0,
    NYR_TASK_READY,
    NYR_TASK_RUNNING,
    NYR_TASK_BLOCKED,
    NYR_TASK_ZOMBIE
} nyr_task_state_t;

typedef void (*nyr_task_entry_t)(void *arg);

typedef struct nyr_task {
    uint64_t tid;
    const char *name;
    uint64_t rsp;
    void *stack_base;
    size_t stack_size;
    nyr_task_state_t state;
    uint64_t ticks_run;
    struct nyr_task *next;
} nyr_task_t;

void nyr_sched_init(void);
nyr_task_t *nyr_task_create(const char *name, nyr_task_entry_t entry, void *arg);
void nyr_sched_yield(void);
void nyr_task_exit(void);
nyr_task_t *nyr_current_task(void);
uint64_t nyr_sched_task_count(void);

/* Cooperative scheduler verification self-test */
bool nyr_sched_self_test(void);

#endif /* NYR_SCHED_H */
