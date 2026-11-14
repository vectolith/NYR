#ifndef NYR_MEMORY_H
#define NYR_MEMORY_H

#include <stdbool.h>
#include <stdint.h>

struct limine_memmap_response;

/*
 * Initialise the physical frame allocator from the Limine memory map.
 * Only complete pages wholly contained in LIMINE_MEMMAP_USABLE regions are
 * eligible. The bitmap itself is carved out of a usable region and reserved.
 * Allocator mutations and accounting queries are serialized by an IRQ-saving SMP spinlock.
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

/* Claim prevents free while a VMM-owned mapping references the frame. */
bool nyr_pmm_claim_frame(uint64_t physical_address);
bool nyr_pmm_release_claim(uint64_t physical_address);

/* True for allocated or VMM-claimed frames, excluding allocator metadata. */
bool nyr_pmm_frame_is_allocated(uint64_t physical_address);

/* A deterministic allocator self-test intended for early boot diagnostics. */
bool nyr_pmm_self_test(void);

#endif
