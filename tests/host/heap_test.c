#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include "../../kernel/include/heap.h"

int main(void) {
    printf("host heap test: initializing...\n");
    nyr_heap_init(0);

    printf("host heap test: running self test...\n");
    if (!nyr_heap_self_test()) {
        fprintf(stderr, "host heap test: nyr_heap_self_test failed\n");
        return 1;
    }

    /* Test recycling / free-list reuse */
    void *p1 = nyr_kmalloc(64);
    assert(p1 != NULL);
    nyr_kfree(p1);

    void *p2 = nyr_kmalloc(64);
    assert(p2 != NULL);
    /* In slab allocators, reallocating the same size class immediately recycles the freed block */
    assert(p1 == p2);
    nyr_kfree(p2);
    nyr_kfree(p2); /* A repeated free must not corrupt the slab free list. */
    void *p3 = nyr_kmalloc(64);
    void *p4 = nyr_kmalloc(64);
    assert(p3 == p2);
    assert(p4 != p3);
    nyr_kfree(p3);
    nyr_kfree(p4);

    /* Test various size classes */
    for (size_t s = 8; s <= 2048; s *= 2) {
        void *p = nyr_kmalloc(s);
        assert(p != NULL);
        assert(((uintptr_t)p & 15) == 0);
        memset(p, 0xCC, s);
        nyr_kfree(p);
    }

    /* Direct-page allocations must stay within the single frame actually
     * obtained from the PMM; unsupported multi-page sizes fail without leaks. */
    const size_t allocated_before_large = nyr_heap_total_allocated();
    const size_t count_before_large = nyr_heap_allocation_count();
    assert(nyr_kmalloc(4096) == NULL);
    assert(nyr_kmalloc(SIZE_MAX) == NULL);
    assert(nyr_heap_total_allocated() == allocated_before_large);
    assert(nyr_heap_allocation_count() == count_before_large);

    void *large = nyr_kmalloc(3000);
    assert(large != NULL);
    assert(((uintptr_t)large & 15) == 0);
    memset(large, 0xA5, 3000);
    nyr_kfree(large);
    assert(nyr_heap_total_allocated() == allocated_before_large);
    assert(nyr_heap_allocation_count() == count_before_large);

    printf("host heap test: ALL TESTS PASSED\n");
    return 0;
}
