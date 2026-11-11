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

#endif
