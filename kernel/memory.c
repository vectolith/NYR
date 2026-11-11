#include "include/memory.h"
#include "include/limine.h"

#include <stddef.h>
#include <stdint.h>

#define NYR_PAGE_SIZE       UINT64_C(4096)
#define NYR_PAGE_MASK       (NYR_PAGE_SIZE - 1)
#define NYR_LOW_MEMORY_END  UINT64_C(0x100000)
#define NYR_MAX_MAP_ENTRIES UINT64_C(4096)

/*
 * Two bits per physical page:
 *   00 = not managed (reserved, bad, firmware, or outside usable RAM)
 *   01 = free
 *   10 = allocated
 *   11 = invalid
 *
 * This makes free() reject addresses in reserved memory instead of silently
 * turning them into allocatable frames.
 */
#define FRAME_STATE_UNMANAGED 0u
#define FRAME_STATE_FREE      1u
#define FRAME_STATE_ALLOCATED 2u
#define FRAME_STATE_INVALID   3u

static uint8_t *frame_states;
static uint64_t frame_count;
static uint64_t metadata_physical;
static uint64_t metadata_pages;
static uint64_t next_hint;
static uint64_t managed_frames;
static uint64_t free_frames;
static uint64_t allocated_frames;
static bool allocator_ready;

static void pmm_serial_write(const char *text) {
    while (*text != '\0') {
        unsigned int timeout = 1000000;
        uint8_t status = 0;
        do {
            __asm__ volatile ("inb %1, %0" : "=a"(status) : "Nd"((uint16_t)0x3fd));
            if (status & 0x20u) break;
        } while (--timeout != 0);
        if (timeout != 0) {
            __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)*text), "Nd"((uint16_t)0x3f8));
        }
        ++text;
    }
}

static bool add_overflow_u64(uint64_t a, uint64_t b, uint64_t *out) {
    if (b > UINT64_MAX - a) {
        return true;
    }
    *out = a + b;
    return false;
}

static bool align_up_page(uint64_t value, uint64_t *out) {
    uint64_t adjusted;
    if (add_overflow_u64(value, NYR_PAGE_MASK, &adjusted)) {
        return false;
    }
    *out = adjusted & ~NYR_PAGE_MASK;
    return true;
}

static uint64_t align_down_page(uint64_t value) {
    return value & ~NYR_PAGE_MASK;
}

static uint8_t frame_state_get(uint64_t frame) {
    const uint64_t byte_index = frame >> 2;
    const unsigned int shift = (unsigned int)((frame & 3u) * 2u);
    return (uint8_t)((frame_states[byte_index] >> shift) & 3u);
}

static void frame_state_set(uint64_t frame, uint8_t state) {
    const uint64_t byte_index = frame >> 2;
    const unsigned int shift = (unsigned int)((frame & 3u) * 2u);
    const uint8_t mask = (uint8_t)(3u << shift);
    frame_states[byte_index] =
        (uint8_t)((frame_states[byte_index] & (uint8_t)~mask) |
                  (uint8_t)((state & 3u) << shift));
}

static bool usable_region_bounds(const struct limine_memmap_entry *entry,
                                 uint64_t *start, uint64_t *end) {
    uint64_t region_end;
    if (entry == NULL || entry->type != LIMINE_MEMMAP_USABLE ||
        entry->length == 0 ||
        add_overflow_u64(entry->base, entry->length, &region_end)) {
        return false;
    }

    uint64_t first = entry->base < NYR_LOW_MEMORY_END
                         ? NYR_LOW_MEMORY_END
                         : entry->base;
    if (!align_up_page(first, &first)) {
        return false;
    }

    const uint64_t last = align_down_page(region_end);
    if (first >= last) {
        return false;
    }

    *start = first;
    *end = last;
    return true;
}

static bool map_entry(const struct limine_memmap_response *map,
                      uint64_t index,
                      const struct limine_memmap_entry **entry) {
    if (map == NULL || map->entries == NULL ||
        index >= map->entry_count || map->entries[index] == NULL) {
        return false;
    }
    *entry = map->entries[index];
    return true;
}

bool nyr_pmm_init(const struct limine_memmap_response *memory_map,
                  uint64_t hhdm_offset) {
    pmm_serial_write("NYR: PMM init entered\r\n");
    if (allocator_ready || memory_map == NULL ||
        memory_map->entries == NULL || memory_map->entry_count == 0 ||
        memory_map->entry_count > NYR_MAX_MAP_ENTRIES) {
        pmm_serial_write("NYR: PMM invalid memory-map response\r\n");
        return false;
    }
    pmm_serial_write("NYR: PMM memory-map response accepted\r\n");

    uint64_t highest_usable_address = 0;
    for (uint64_t i = 0; i < memory_map->entry_count; ++i) {
        const struct limine_memmap_entry *entry;
        if (!map_entry(memory_map, i, &entry)) {
            return false;
        }
        if (entry->type != LIMINE_MEMMAP_USABLE || entry->length == 0) {
            continue;
        }

        uint64_t end;
        if (add_overflow_u64(entry->base, entry->length, &end)) {
            return false;
        }
        if (end > highest_usable_address) {
            highest_usable_address = end;
        }
    }

    if (highest_usable_address <= NYR_LOW_MEMORY_END) {
        pmm_serial_write("NYR: PMM no usable memory above 1 MiB\r\n");
        return false;
    }
    pmm_serial_write("NYR: PMM usable memory range found\r\n");

    frame_count = highest_usable_address / NYR_PAGE_SIZE;
    if ((highest_usable_address & NYR_PAGE_MASK) != 0) {
        ++frame_count;
    }

    /* Four frames are represented by each byte (two bits per frame). */
    const uint64_t state_bytes_u64 =
        frame_count / 4u + ((frame_count & 3u) != 0 ? 1u : 0u);
    if (state_bytes_u64 == 0 || state_bytes_u64 > (uint64_t)SIZE_MAX) {
        return false;
    }

    const uint64_t required_pages =
        state_bytes_u64 / NYR_PAGE_SIZE +
        ((state_bytes_u64 & NYR_PAGE_MASK) != 0 ? 1u : 0u);
    if (required_pages == 0) {
        return false;
    }

    /*
     * Place metadata wholly inside one usable region. The selected frames are
     * marked allocated after the usable ranges have been made free.
     */
    bool metadata_found = false;
    uint64_t chosen_start = 0;
    for (uint64_t i = 0; i < memory_map->entry_count; ++i) {
        const struct limine_memmap_entry *entry;
        uint64_t start, end;
        if (!map_entry(memory_map, i, &entry)) {
            return false;
        }
        if (!usable_region_bounds(entry, &start, &end)) {
            continue;
        }
        const uint64_t available_pages = (end - start) / NYR_PAGE_SIZE;
        if (available_pages >= required_pages) {
            chosen_start = start;
            metadata_found = true;
            break;
        }
    }
    if (!metadata_found) {
        pmm_serial_write("NYR: PMM no space for frame-state metadata\r\n");
        return false;
    }
    pmm_serial_write("NYR: PMM metadata placement found\r\n");

    uint64_t metadata_bytes;
    if (required_pages > UINT64_MAX / NYR_PAGE_SIZE) {
        return false;
    }
    metadata_bytes = required_pages * NYR_PAGE_SIZE;
    if (metadata_bytes > (uint64_t)SIZE_MAX ||
        chosen_start > UINT64_MAX - hhdm_offset) {
        return false;
    }

    metadata_physical = chosen_start;
    metadata_pages = required_pages;
    frame_states = (uint8_t *)(uintptr_t)(hhdm_offset + metadata_physical);

    /* Mark every frame unmanaged first; only usable pages are subsequently freed. */
    for (uint64_t i = 0; i < state_bytes_u64; ++i) {
        frame_states[i] = 0;
    }

    pmm_serial_write("NYR: PMM frame-state storage cleared\r\n");
    managed_frames = 0;
    free_frames = 0;
    allocated_frames = 0;

    for (uint64_t i = 0; i < memory_map->entry_count; ++i) {
        const struct limine_memmap_entry *entry;
        uint64_t start, end;
        if (!map_entry(memory_map, i, &entry)) {
            return false;
        }
        if (!usable_region_bounds(entry, &start, &end)) {
            continue;
        }

        for (uint64_t address = start; address < end;
             address += NYR_PAGE_SIZE) {
            const uint64_t frame = address / NYR_PAGE_SIZE;
            if (frame >= frame_count ||
                frame_state_get(frame) != FRAME_STATE_UNMANAGED) {
                /* Overlapping usable entries are malformed and unsafe. */
                return false;
            }
            frame_state_set(frame, FRAME_STATE_FREE);
            ++managed_frames;
            ++free_frames;
        }
    }

    pmm_serial_write("NYR: PMM usable frames marked\r\n");
    const uint64_t metadata_first = metadata_physical / NYR_PAGE_SIZE;
    for (uint64_t i = 0; i < metadata_pages; ++i) {
        const uint64_t frame = metadata_first + i;
        if (frame >= frame_count ||
            frame_state_get(frame) != FRAME_STATE_FREE) {
            return false;
        }
        frame_state_set(frame, FRAME_STATE_ALLOCATED);
        --free_frames;
        ++allocated_frames;
    }

    pmm_serial_write("NYR: PMM metadata reserved\r\n");
    next_hint = 0;
    allocator_ready = true;
    return nyr_pmm_get_stats(&(struct nyr_pmm_stats){0});
}

uint64_t nyr_pmm_alloc_frame(void) {
    if (!allocator_ready || free_frames == 0 || frame_count == 0) {
        return 0;
    }

    uint64_t frame = next_hint;
    for (uint64_t scanned = 0; scanned < frame_count; ++scanned) {
        if (frame_state_get(frame) == FRAME_STATE_FREE) {
            frame_state_set(frame, FRAME_STATE_ALLOCATED);
            --free_frames;
            ++allocated_frames;
            next_hint = frame + 1u;
            if (next_hint == frame_count) {
                next_hint = 0;
            }
            return frame * NYR_PAGE_SIZE;
        }

        ++frame;
        if (frame == frame_count) {
            frame = 0;
        }
    }

    /* Keep counters honest if the bitmap and counters ever disagree. */
    free_frames = 0;
    return 0;
}

bool nyr_pmm_free_frame(uint64_t physical_address) {
    if (!allocator_ready || (physical_address & NYR_PAGE_MASK) != 0) {
        return false;
    }

    const uint64_t frame = physical_address / NYR_PAGE_SIZE;
    if (frame >= frame_count ||
        (physical_address >= metadata_physical &&
         physical_address - metadata_physical <
             metadata_pages * NYR_PAGE_SIZE) ||
        frame_state_get(frame) != FRAME_STATE_ALLOCATED) {
        return false;
    }

    frame_state_set(frame, FRAME_STATE_FREE);
    ++free_frames;
    --allocated_frames;
    if (frame < next_hint) {
        next_hint = frame;
    }
    return true;
}

bool nyr_pmm_get_stats(struct nyr_pmm_stats *stats) {
    if (!allocator_ready || stats == NULL) {
        return false;
    }

    stats->managed_frames = managed_frames;
    stats->free_frames = free_frames;
    stats->allocated_frames = allocated_frames;
    return true;
}

bool nyr_pmm_self_test(void) {
    const uint64_t before = free_frames;
    const uint64_t first = nyr_pmm_alloc_frame();
    const uint64_t second = nyr_pmm_alloc_frame();
    const uint64_t third = nyr_pmm_alloc_frame();

    if (first == 0 || second == 0 || third == 0 ||
        first == second || first == third || second == third ||
        (first & NYR_PAGE_MASK) != 0 ||
        (second & NYR_PAGE_MASK) != 0 ||
        (third & NYR_PAGE_MASK) != 0) {
        return false;
    }

    if (!nyr_pmm_free_frame(first) ||
        nyr_pmm_free_frame(first) ||
        !nyr_pmm_free_frame(second) ||
        !nyr_pmm_free_frame(third)) {
        return false;
    }

    if (free_frames != before) {
        return false;
    }

    const uint64_t recycled = nyr_pmm_alloc_frame();
    if (recycled == 0 || !nyr_pmm_free_frame(recycled)) {
        return false;
    }

    return free_frames == before;
}
