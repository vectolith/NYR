#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../kernel/include/limine.h"
#include "../../kernel/include/memory.h"

#define PAGE_SIZE UINT64_C(4096)
#define RAM_BASE  UINT64_C(0x100000)
#define RAM_SIZE  (UINT64_C(16) * 1024u * 1024u)

static int run_valid(void) {
    void *ram = aligned_alloc((size_t)PAGE_SIZE, (size_t)RAM_SIZE);
    if (ram == NULL) {
        fputs("host PMM test: aligned_alloc failed\n", stderr);
        return 1;
    }
    memset(ram, 0xa5, (size_t)RAM_SIZE);

    struct limine_memmap_entry usable = {
        .base = RAM_BASE,
        .length = RAM_SIZE,
        .type = LIMINE_MEMMAP_USABLE
    };
    struct limine_memmap_entry *entries[] = { &usable };
    struct limine_memmap_response map = {
        .revision = 0,
        .entry_count = 1,
        .entries = entries
    };

    const uint64_t hhdm_offset = (uint64_t)(uintptr_t)ram - RAM_BASE;
    if (!nyr_pmm_init(&map, hhdm_offset)) {
        fputs("host PMM test: valid map rejected\n", stderr);
        free(ram);
        return 1;
    }

    const uint64_t expected_managed = RAM_SIZE / PAGE_SIZE;
    if (nyr_pmm_managed_frames() != expected_managed ||
        nyr_pmm_allocated_frames() != 1 ||
        nyr_pmm_free_frames() != expected_managed - 1 ||
        !nyr_pmm_self_test()) {
        fputs("host PMM test: counters or allocator self-test failed\n", stderr);
        free(ram);
        return 1;
    }

    puts("host PMM test: valid map, metadata reservation and allocator self-test passed");
    free(ram);
    return 0;
}

static int run_overlap(void) {
    struct limine_memmap_entry usable = {
        .base = RAM_BASE,
        .length = UINT64_C(8) * 1024u * 1024u,
        .type = LIMINE_MEMMAP_USABLE
    };
    struct limine_memmap_entry reserved = {
        .base = RAM_BASE + UINT64_C(0x200000),
        .length = UINT64_C(0x100000),
        .type = LIMINE_MEMMAP_RESERVED
    };
    struct limine_memmap_entry *entries[] = { &usable, &reserved };
    struct limine_memmap_response map = {
        .revision = 0,
        .entry_count = 2,
        .entries = entries
    };

    if (nyr_pmm_init(&map, 0)) {
        fputs("host PMM test: overlapping map was accepted\n", stderr);
        return 1;
    }
    puts("host PMM test: overlapping map rejected");
    return 0;
}

static int run_overflow(void) {
    struct limine_memmap_entry invalid = {
        .base = UINT64_MAX - UINT64_C(0x1000),
        .length = UINT64_C(0x2000),
        .type = LIMINE_MEMMAP_USABLE
    };
    struct limine_memmap_entry *entries[] = { &invalid };
    struct limine_memmap_response map = {
        .revision = 0,
        .entry_count = 1,
        .entries = entries
    };

    if (nyr_pmm_init(&map, 0)) {
        fputs("host PMM test: overflowing map was accepted\n", stderr);
        return 1;
    }
    puts("host PMM test: overflowing map rejected");
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s valid|overlap|overflow\n", argv[0]);
        return 2;
    }
    if (strcmp(argv[1], "valid") == 0) return run_valid();
    if (strcmp(argv[1], "overlap") == 0) return run_overlap();
    if (strcmp(argv[1], "overflow") == 0) return run_overflow();
    fprintf(stderr, "unknown test mode: %s\n", argv[1]);
    return 2;
}
