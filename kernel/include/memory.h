#ifndef NYR_MEMORY_H
#define NYR_MEMORY_H

#include <stdbool.h>
#include <stdint.h>

struct limine_memmap_response;

/*
 * Initialise the physical frame allocator from the Limine memory map.
 * Only complete pages wholly contained in LIMINE_MEMMAP_USABLE regions are
 * eligible. The bitmap itself is carved out of a usable region and reserved.
 * This implementation is single-CPU until interrupt-safe locking is added.
 */
bool nyr_pmm_init(const struct limine_memmap_response *memory_map,
                  uint64_t hhdm_offset);

/* Return a 4 KiB-aligned physical address, or 0 on exhaustion/uninitialised. */
uint64_t nyr_pmm_alloc_frame(void);

/* Release a frame previously returned by nyr_pmm_alloc_frame. */
bool nyr_pmm_free_frame(uint64_t physical_address);

uint64_t nyr_pmm_managed_frames(void);
uint64_t nyr_pmm_free_frames(void);
uint64_t nyr_pmm_allocated_frames(void);

/* True only for a live PMM allocation, excluding allocator metadata pages. */
bool nyr_pmm_frame_is_allocated(uint64_t physical_address);

/* A deterministic allocator self-test intended for early boot diagnostics. */
bool nyr_pmm_self_test(void);

#endif
