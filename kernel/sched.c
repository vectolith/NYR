#include "include/sched.h"
#include "include/heap.h"
#include "include/lock.h"
#include "include/serial.h"

extern void nyr_switch_context(uint64_t *prev_rsp, uint64_t next_rsp);
extern void nyr_task_starter(void);

#define NYR_TASK_STACK_SIZE 16384u
#define MAX_TASKS 32u

static nyr_task_t *task_list = NULL;
static nyr_task_t *current_task = NULL;
static nyr_task_t bsp_idle_task;
static uint64_t next_tid = 1;
static struct nyr_spinlock sched_lock = NYR_SPINLOCK_INITIALIZER;
static bool sched_ready = false;

/* Self-test tracking */
static volatile uint64_t test_worker_counter = 0;

void nyr_sched_init(void) {
    bsp_idle_task.tid = 0;
    bsp_idle_task.name = "bsp_idle";
    bsp_idle_task.rsp = 0;
    bsp_idle_task.stack_base = NULL;
    bsp_idle_task.stack_size = 0;
    bsp_idle_task.state = NYR_TASK_RUNNING;
    bsp_idle_task.ticks_run = 0;
    bsp_idle_task.next = NULL;

    current_task = &bsp_idle_task;
    task_list = &bsp_idle_task;
    sched_ready = true;
}

nyr_task_t *nyr_task_create(const char *name, nyr_task_entry_t entry, void *arg) {
    if (!sched_ready || entry == NULL) {
        return NULL;
    }

    nyr_spinlock_acquire(&sched_lock);

    nyr_task_t *task = (nyr_task_t *)nyr_kmalloc(sizeof(nyr_task_t));
    if (task == NULL) {
        nyr_spinlock_release(&sched_lock);
        return NULL;
    }

    void *stack = nyr_kmalloc(NYR_TASK_STACK_SIZE);
    if (stack == NULL) {
        nyr_kfree(task);
        nyr_spinlock_release(&sched_lock);
        return NULL;
    }

    task->tid = next_tid++;
    task->name = name ? name : "task";
    task->stack_base = stack;
    task->stack_size = NYR_TASK_STACK_SIZE;
    task->state = NYR_TASK_READY;
    task->ticks_run = 0;

    /*
     * Initialize the stack frame for nyr_switch_context.
     * The stack top must be 16-byte aligned.
     */
    uint64_t *sp = (uint64_t *)((uintptr_t)stack + NYR_TASK_STACK_SIZE);
    /* Align downwards */
    sp = (uint64_t *)((uintptr_t)sp & ~15u);

    /* Frame layout matching nyr_switch_context pop order:
     * ret-address (nyr_task_starter), rflags, rbx, rbp, r12, r13, r14, r15
     */
    *(--sp) = (uint64_t)nyr_task_starter; /* Return IP */
    *(--sp) = 0x202u;                     /* rflags (IF enabled) */
    *(--sp) = 0;                          /* rbx */
    *(--sp) = 0;                          /* rbp */
    *(--sp) = (uint64_t)entry;            /* r12: entry function */
    *(--sp) = (uint64_t)arg;              /* r13: argument */
    *(--sp) = 0;                          /* r14 */
    *(--sp) = 0;                          /* r15 */

    task->rsp = (uint64_t)sp;

    /* Enqueue to task list */
    task->next = task_list;
    task_list = task;

    nyr_spinlock_release(&sched_lock);
    return task;
}

void nyr_sched_yield(void) {
    if (!sched_ready || current_task == NULL) {
        return;
    }

    nyr_spinlock_acquire(&sched_lock);

    /* Round-robin search for next READY task */
    nyr_task_t *prev = current_task;
    nyr_task_t *next = prev->next ? prev->next : task_list;

    while (next != prev) {
        if (next->state == NYR_TASK_READY) {
            break;
        }
        next = next->next ? next->next : task_list;
    }

    if (next == prev || next->state != NYR_TASK_READY) {
        /* No other ready task found */
        nyr_spinlock_release(&sched_lock);
        return;
    }

    if (prev->state == NYR_TASK_RUNNING) {
        prev->state = NYR_TASK_READY;
    }
    next->state = NYR_TASK_RUNNING;
    current_task = next;

    nyr_spinlock_release(&sched_lock);

    /* Perform the context switch */
    nyr_switch_context(&prev->rsp, next->rsp);
}

void nyr_task_exit(void) {
    if (current_task != NULL) {
        current_task->state = NYR_TASK_ZOMBIE;
    }
    for (;;) {
        nyr_sched_yield();
        __asm__ volatile ("pause");
    }
}

nyr_task_t *nyr_current_task(void) {
    return current_task;
}

uint64_t nyr_sched_task_count(void) {
    uint64_t count = 0;
    nyr_task_t *t = task_list;
    while (t != NULL) {
        count++;
        t = t->next;
    }
    return count;
}

static void test_worker_entry(void *arg) {
    uint64_t delta = (uint64_t)(uintptr_t)arg;
    test_worker_counter += delta;
    /* Yield back to caller */
    nyr_sched_yield();
    test_worker_counter += delta;
}

bool nyr_sched_self_test(void) {
    test_worker_counter = 0;
    nyr_task_t *worker = nyr_task_create("test_worker", test_worker_entry, (void *)(uintptr_t)42);
    if (worker == NULL) {
        return false;
    }

    /* Yield to the worker */
    nyr_sched_yield();

    if (test_worker_counter != 42) {
        return false;
    }

    /* Yield again to allow worker to finish */
    nyr_sched_yield();

    if (test_worker_counter != 84) {
        return false;
    }

    return true;
}
