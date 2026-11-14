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

    /* Test various size classes */
    for (size_t s = 8; s <= 2048; s *= 2) {
        void *p = nyr_kmalloc(s);
        assert(p != NULL);
        assert(((uintptr_t)p & 15) == 0);
        memset(p, 0xCC, s);
        nyr_kfree(p);
    }

    printf("host heap test: ALL TESTS PASSED\n");
    return 0;
}
