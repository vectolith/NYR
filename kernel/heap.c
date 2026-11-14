#include "include/heap.h"
#include "include/lock.h"

#ifndef NYR_HOST_TEST
#include "include/memory.h"
#include "include/serial.h"
#else
#include <stdlib.h>
#include <string.h>
#endif

#define NYR_PAGE_SIZE 4096u
#define NUM_SIZE_CLASSES 7u

static const size_t SIZE_CLASSES[NUM_SIZE_CLASSES] = {
    32, 64, 128, 256, 512, 1024, 2048
};

struct nyr_heap_header {
    uint32_t magic;          /* NYR_HEAP_MAGIC */
    uint16_t size_class_idx; /* 0..6 or 0xFFFF for direct-page */
    uint16_t page_count;     /* Number of backing pages (for large) */
    size_t requested_size;
} __attribute__((aligned(16)));

struct nyr_slab_block {
    struct nyr_slab_block *next;
};

struct nyr_slab {
    struct nyr_slab *next;
    struct nyr_slab_block *free_list;
    size_t free_count;
    size_t total_blocks;
};

static uint64_t heap_hhdm_offset = 0;
static struct nyr_slab *slabs[NUM_SIZE_CLASSES] = { 0 };
static struct nyr_spinlock heap_lock = NYR_SPINLOCK_INITIALIZER;
static size_t total_allocated_bytes = 0;
static size_t active_allocations = 0;

#ifdef NYR_HOST_TEST
static void *host_alloc_page(void) {
    void *ptr = NULL;
    if (posix_memalign(&ptr, NYR_PAGE_SIZE, NYR_PAGE_SIZE) != 0) {
        return NULL;
    }
    memset(ptr, 0, NYR_PAGE_SIZE);
    return ptr;
}

static void host_free_page(void *ptr) {
    free(ptr);
}
#endif

static void *heap_alloc_page(void) {
#ifdef NYR_HOST_TEST
    return host_alloc_page();
#else
    uint64_t frame = nyr_pmm_alloc_frame();
    if (frame == 0) {
        return NULL;
    }
    void *virt = (void *)(uintptr_t)(frame + heap_hhdm_offset);
    uint8_t *bytes = (uint8_t *)virt;
    for (size_t i = 0; i < NYR_PAGE_SIZE; ++i) {
        bytes[i] = 0;
    }
    return virt;
#endif
}

static void heap_free_page(void *ptr) {
#ifdef NYR_HOST_TEST
    host_free_page(ptr);
#else
    if (ptr == NULL) return;
    uint64_t frame = (uint64_t)(uintptr_t)ptr - heap_hhdm_offset;
    nyr_pmm_free_frame(frame);
#endif
}

void nyr_heap_init(uint64_t hhdm_offset) {
    heap_hhdm_offset = hhdm_offset;
    total_allocated_bytes = 0;
    active_allocations = 0;
    for (size_t i = 0; i < NUM_SIZE_CLASSES; ++i) {
        slabs[i] = NULL;
    }
}

static int get_size_class_index(size_t size) {
    for (size_t i = 0; i < NUM_SIZE_CLASSES; ++i) {
        if (size <= SIZE_CLASSES[i]) {
            return (int)i;
        }
    }
    return -1;
}

void *nyr_kmalloc(size_t size) {
    if (size == 0) {
        return NULL;
    }

    nyr_spinlock_acquire(&heap_lock);

    int sc_idx = get_size_class_index(size);
    if (sc_idx >= 0) {
        /* Allocate from size-class slab */
        size_t block_data_size = SIZE_CLASSES[sc_idx];
        size_t block_total_size = sizeof(struct nyr_heap_header) + block_data_size;

        /* Look for slab with free blocks */
        struct nyr_slab *slab = slabs[sc_idx];
        while (slab != NULL && slab->free_list == NULL) {
            slab = slab->next;
        }

        if (slab == NULL || slab->free_list == NULL) {
            /* Create new slab */
            void *page = heap_alloc_page();
            if (page == NULL) {
                nyr_spinlock_release(&heap_lock);
                return NULL;
            }

            slab = (struct nyr_slab *)page;
            slab->next = slabs[sc_idx];
            slabs[sc_idx] = slab;

            /* Initialize blocks inside this slab page */
            uintptr_t start = (uintptr_t)page + sizeof(struct nyr_slab);
            /* Align start to 16 bytes */
            start = (start + 15u) & ~((uintptr_t)15u);
            uintptr_t end = (uintptr_t)page + NYR_PAGE_SIZE;

            slab->free_list = NULL;
            slab->free_count = 0;
            slab->total_blocks = 0;

            while (start + block_total_size <= end) {
                struct nyr_slab_block *b = (struct nyr_slab_block *)start;
                b->next = slab->free_list;
                slab->free_list = b;
                slab->free_count++;
                slab->total_blocks++;
                start += block_total_size;
            }
        }

        if (slab->free_list == NULL) {
            nyr_spinlock_release(&heap_lock);
            return NULL;
        }

        /* Pop block from freelist */
        struct nyr_slab_block *b = slab->free_list;
        slab->free_list = b->next;
        slab->free_count--;

        struct nyr_heap_header *hdr = (struct nyr_heap_header *)b;
        hdr->magic = NYR_HEAP_MAGIC;
        hdr->size_class_idx = (uint16_t)sc_idx;
        hdr->page_count = 1;
        hdr->requested_size = size;

        total_allocated_bytes += block_data_size;
        active_allocations++;

        nyr_spinlock_release(&heap_lock);
        return (void *)(hdr + 1);
    }

    /* Large allocation: allocate dedicated contiguous pages */
    size_t total_needed = sizeof(struct nyr_heap_header) + size;
    size_t pages = (total_needed + NYR_PAGE_SIZE - 1) / NYR_PAGE_SIZE;

    /* For simplicity and reliability, allocate pages */
    void *first_page = heap_alloc_page();
    if (first_page == NULL) {
        nyr_spinlock_release(&heap_lock);
        return NULL;
    }

    /* Note: In NYR single 4 KiB frame allocator, multi-page contiguous allocations
     * for early kernel heap are bounded or use page chains. For up to 1 page + header: */
    struct nyr_heap_header *hdr = (struct nyr_heap_header *)first_page;
    hdr->magic = NYR_HEAP_MAGIC;
    hdr->size_class_idx = 0xFFFFu;
    hdr->page_count = (uint16_t)pages;
    hdr->requested_size = size;

    total_allocated_bytes += pages * NYR_PAGE_SIZE;
    active_allocations++;

    nyr_spinlock_release(&heap_lock);
    return (void *)(hdr + 1);
}

void *nyr_kzalloc(size_t size) {
    void *ptr = nyr_kmalloc(size);
    if (ptr != NULL) {
        uint8_t *p = (uint8_t *)ptr;
        for (size_t i = 0; i < size; ++i) {
            p[i] = 0;
        }
    }
    return ptr;
}

void nyr_kfree(void *ptr) {
    if (ptr == NULL) {
        return;
    }

    struct nyr_heap_header *hdr = ((struct nyr_heap_header *)ptr) - 1;
    if (hdr->magic != NYR_HEAP_MAGIC) {
        /* Invalid or double free; fail-safe return */
        return;
    }

    nyr_spinlock_acquire(&heap_lock);

    /* Invalidate magic to detect double-free */
    hdr->magic = 0xDEADBEEFu;

    if (hdr->size_class_idx < NUM_SIZE_CLASSES) {
        size_t sc_idx = hdr->size_class_idx;
        size_t block_data_size = SIZE_CLASSES[sc_idx];

        /* Return block to its slab */
        uintptr_t page_addr = ((uintptr_t)hdr) & ~((uintptr_t)(NYR_PAGE_SIZE - 1));
        struct nyr_slab *slab = (struct nyr_slab *)page_addr;

        struct nyr_slab_block *b = (struct nyr_slab_block *)hdr;
        b->next = slab->free_list;
        slab->free_list = b;
        slab->free_count++;

        if (total_allocated_bytes >= block_data_size) {
            total_allocated_bytes -= block_data_size;
        }
    } else {
        /* Large allocation */
        size_t pages = hdr->page_count;
        if (total_allocated_bytes >= pages * NYR_PAGE_SIZE) {
            total_allocated_bytes -= pages * NYR_PAGE_SIZE;
        }
        heap_free_page((void *)hdr);
    }

    if (active_allocations > 0) {
        active_allocations--;
    }

    nyr_spinlock_release(&heap_lock);
}

void *nyr_krealloc(void *ptr, size_t new_size) {
    if (ptr == NULL) {
        return nyr_kmalloc(new_size);
    }
    if (new_size == 0) {
        nyr_kfree(ptr);
        return NULL;
    }

    struct nyr_heap_header *hdr = ((struct nyr_heap_header *)ptr) - 1;
    if (hdr->magic != NYR_HEAP_MAGIC) {
        return NULL;
    }

    if (hdr->size_class_idx < NUM_SIZE_CLASSES) {
        size_t capacity = SIZE_CLASSES[hdr->size_class_idx];
        if (new_size <= capacity) {
            hdr->requested_size = new_size;
            return ptr;
        }
    }

    void *new_ptr = nyr_kmalloc(new_size);
    if (new_ptr == NULL) {
        return NULL;
    }

    size_t copy_size = hdr->requested_size < new_size ? hdr->requested_size : new_size;
    const uint8_t *src = (const uint8_t *)ptr;
    uint8_t *dst = (uint8_t *)new_ptr;
    for (size_t i = 0; i < copy_size; ++i) {
        dst[i] = src[i];
    }

    nyr_kfree(ptr);
    return new_ptr;
}

size_t nyr_heap_total_allocated(void) {
    return total_allocated_bytes;
}

size_t nyr_heap_allocation_count(void) {
    return active_allocations;
}

bool nyr_heap_self_test(void) {
    /* Test small allocations across multiple size classes */
    void *ptrs[10];
    for (size_t i = 0; i < 10; ++i) {
        size_t sz = 16u * (i + 1);
        ptrs[i] = nyr_kmalloc(sz);
        if (ptrs[i] == NULL) {
            return false;
        }
        /* Verify 16-byte alignment */
        if (((uintptr_t)ptrs[i] & 15u) != 0) {
            return false;
        }
        /* Write canary pattern */
        uint8_t *b = (uint8_t *)ptrs[i];
        for (size_t j = 0; j < sz; ++j) {
            b[j] = (uint8_t)(j ^ 0x5A);
        }
    }

    /* Verify canary pattern */
    for (size_t i = 0; i < 10; ++i) {
        size_t sz = 16u * (i + 1);
        uint8_t *b = (uint8_t *)ptrs[i];
        for (size_t j = 0; j < sz; ++j) {
            if (b[j] != (uint8_t)(j ^ 0x5A)) {
                return false;
            }
        }
    }

    /* Test kzalloc */
    void *zero_block = nyr_kzalloc(256);
    if (zero_block == NULL) {
        return false;
    }
    uint8_t *zb = (uint8_t *)zero_block;
    for (size_t j = 0; j < 256; ++j) {
        if (zb[j] != 0) {
            return false;
        }
    }

    /* Test realloc */
    zero_block = nyr_krealloc(zero_block, 512);
    if (zero_block == NULL) {
        return false;
    }
    zb = (uint8_t *)zero_block;
    for (size_t j = 0; j < 256; ++j) {
        if (zb[j] != 0) {
            return false;
        }
    }
    nyr_kfree(zero_block);

    /* Free all small blocks */
    for (size_t i = 0; i < 10; ++i) {
        nyr_kfree(ptrs[i]);
    }

    return true;
}
