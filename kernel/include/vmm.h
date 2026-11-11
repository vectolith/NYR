#ifndef NYR_VMM_H
#define NYR_VMM_H

#include <stdbool.h>
#include <stdint.h>

struct nyr_vmm_translation {
    uint64_t physical_address;
    uint64_t page_size;
    bool writable;
    bool user_accessible;
    bool executable;
    bool uncached;
};

/*
 * Translate using an explicit four-level page-table root. Exposed so the
 * page-table walker can be tested against synthetic tables on the host.
 */
bool nyr_vmm_translate_from_root(uint64_t root_physical,
                                 uint64_t hhdm_offset,
                                 uint64_t virtual_address,
                                 struct nyr_vmm_translation *translation);

/* Translate through the currently active CR3 page-table root. */
bool nyr_vmm_translate_current(uint64_t hhdm_offset,
                               uint64_t virtual_address,
                               struct nyr_vmm_translation *translation);

/* Validate kernel text, the active stack, and a PMM-backed HHDM mapping. */
bool nyr_vmm_self_test(uint64_t hhdm_offset);

enum nyr_vmm_map_flags {
    NYR_VMM_MAP_WRITE = 1u << 0,
    NYR_VMM_MAP_USER = 1u << 1,
    NYR_VMM_MAP_EXECUTE = 1u << 2,
    NYR_VMM_MAP_UNCACHED = 1u << 3
};

enum nyr_vmm_page_ownership {
    NYR_VMM_PAGE_BORROWED = 0,
    NYR_VMM_PAGE_OWNED = 1
};

struct nyr_vmm_unmapped_page {
    uint64_t physical_address;
    enum nyr_vmm_page_ownership ownership;
};

/* Enable and verify architectural NX support before using execute permissions. */
bool nyr_vmm_enable_nx(void);

/*
 * Map one 4 KiB page in the active address space. OWNED transfers a PMM frame
 * to the mapping on success; BORROWED leaves physical-page lifetime with the
 * caller. OWNED frames are PMM-claimed so free() rejects concurrent release.
 * W+X mappings are rejected. Existing huge-page mappings are not split.
 */
bool nyr_vmm_map_page(uint64_t hhdm_offset,
                      uint64_t virtual_address,
                      uint64_t physical_address,
                      uint32_t flags,
                      enum nyr_vmm_page_ownership ownership);

/*
 * Map a contiguous physical range of 4 KiB pages. Partial installation is
 * rolled back; the caller retains ownership of frames after rollback.
 */
bool nyr_vmm_map_range(uint64_t hhdm_offset,
                       uint64_t virtual_address,
                       uint64_t physical_address,
                       uint64_t page_count,
                       uint32_t flags,
                       enum nyr_vmm_page_ownership ownership);

/* Change one existing 4 KiB mapping's leaf permissions; huge leaves are rejected. */
bool nyr_vmm_protect_page(uint64_t hhdm_offset,
                          uint64_t virtual_address,
                          uint32_t flags);

/* Remove one 4 KiB mapping and return its physical page and ownership. */
bool nyr_vmm_unmap_page(uint64_t hhdm_offset,
                        uint64_t virtual_address,
                        struct nyr_vmm_unmapped_page *unmapped);

/* Validate kernel text, stack, HHDM, and map/unmap ownership transitions. */
bool nyr_vmm_self_test(uint64_t hhdm_offset);

#endif
