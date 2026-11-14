#ifndef NYR_HEAP_H
#define NYR_HEAP_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define NYR_HEAP_MAGIC 0x48454150u /* 'HEAP' */

/* Initialize the kernel heap allocator with the higher-half direct map offset */
void nyr_heap_init(uint64_t hhdm_offset);

/* Allocate size bytes of memory from the kernel heap (16-byte aligned) */
void *nyr_kmalloc(size_t size);

/* Allocate and zero size bytes of memory */
void *nyr_kzalloc(size_t size);

/* Free previously allocated heap memory */
void nyr_kfree(void *ptr);

/* Resize an existing heap allocation */
void *nyr_krealloc(void *ptr, size_t new_size);

/* Heap diagnostic counters */
size_t nyr_heap_total_allocated(void);
size_t nyr_heap_allocation_count(void);

/* Comprehensive kernel heap self-test */
bool nyr_heap_self_test(void);

#endif /* NYR_HEAP_H */
