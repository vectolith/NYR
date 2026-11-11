#include "include/vmm.h"
#include "include/memory.h"

#include <stddef.h>
#include <stdint.h>

#define NYR_PAGE_SIZE       UINT64_C(4096)
#define NYR_PAGE_MASK       (NYR_PAGE_SIZE - 1u)
#define X86_PTE_PRESENT     (UINT64_C(1) << 0)
#define X86_PTE_WRITABLE    (UINT64_C(1) << 1)
#define X86_PTE_USER        (UINT64_C(1) << 2)
#define X86_PTE_HUGE        (UINT64_C(1) << 7)
#define X86_PTE_OWNED_TABLE (UINT64_C(1) << 9)
#define X86_PTE_OWNED_PAGE  (UINT64_C(1) << 10)
#define X86_PTE_NO_EXECUTE  (UINT64_C(1) << 63)
#define X86_PTE_ADDRESS     UINT64_C(0x000ffffffffff000)
#define X86_1G_ADDRESS      UINT64_C(0x000fffffc0000000)
#define X86_2M_ADDRESS      UINT64_C(0x000fffffffe00000)
#define X86_1G_SIZE         (UINT64_C(1) << 30)
#define X86_2M_SIZE         (UINT64_C(1) << 21)
#define X86_1G_RESERVED     UINT64_C(0x000000003fffe000)
#define X86_2M_RESERVED     UINT64_C(0x00000000001fe000)
#define X86_EFER_MSR        UINT32_C(0xc0000080)
#define X86_EFER_NXE        (UINT64_C(1) << 11)
#define X86_CPUID_NX        (UINT32_C(1) << 20)

static bool canonical_48(uint64_t address) {
    const uint64_t upper = address >> 48;
    const bool sign = ((address >> 47) & 1u) != 0;
    return sign ? upper == UINT64_C(0xffff) : upper == 0;
}

static volatile uint64_t *table_pointer(uint64_t physical,
                                       uint64_t hhdm_offset) {
    if ((physical & NYR_PAGE_MASK) != 0 ||
        (physical & ~X86_PTE_ADDRESS) != 0 ||
        hhdm_offset > UINT64_MAX - physical) {
        return NULL;
    }
    const uint64_t virtual_address = hhdm_offset + physical;
    if (!canonical_48(virtual_address) ||
        (virtual_address & NYR_PAGE_MASK) != 0) {
        return NULL;
    }
    return (volatile uint64_t *)(uintptr_t)virtual_address;
}

static bool read_present_entry(volatile uint64_t *table,
                               uint16_t index,
                               uint64_t *entry) {
    if (table == NULL || entry == NULL || index >= 512u) {
        return false;
    }
    const uint64_t value = table[index];
    if ((value & X86_PTE_PRESENT) == 0) {
        return false;
    }
    *entry = value;
    return true;
}

static uint64_t active_root(void) {
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    return cr3 & X86_PTE_ADDRESS;
}

static uint64_t irq_save_disable(void) {
    uint64_t rflags;
    __asm__ volatile ("pushfq; popq %0; cli"
                      : "=r"(rflags) : : "memory");
    return rflags;
}

static void irq_restore(uint64_t rflags) {
    __asm__ volatile ("pushq %0; popfq"
                      : : "r"(rflags) : "memory", "cc");
}

static void invalidate_page(uint64_t virtual_address) {
    __asm__ volatile ("invlpg (%0)" : : "r"((uintptr_t)virtual_address) : "memory");
}

static bool allocate_table(uint64_t hhdm_offset,
                           uint64_t *physical,
                           volatile uint64_t **table) {
    const uint64_t frame = nyr_pmm_alloc_frame();
    if (frame == 0) {
        return false;
    }
    volatile uint64_t *pointer = table_pointer(frame, hhdm_offset);
    if (pointer == NULL) {
        (void)nyr_pmm_free_frame(frame);
        return false;
    }
    for (uint16_t i = 0; i < 512u; ++i) {
        pointer[i] = 0;
    }
    *physical = frame;
    *table = pointer;
    return true;
}

static bool table_is_empty(volatile uint64_t *table) {
    for (uint16_t i = 0; i < 512u; ++i) {
        if ((table[i] & X86_PTE_PRESENT) != 0) {
            return false;
        }
    }
    return true;
}

bool nyr_vmm_enable_nx(void) {
    uint32_t maximum_leaf, unused_b, unused_c, unused_d;
    __asm__ volatile ("cpuid"
                      : "=a"(maximum_leaf), "=b"(unused_b),
                        "=c"(unused_c), "=d"(unused_d)
                      : "a"(UINT32_C(0x80000000)), "c"(0));
    if (maximum_leaf < UINT32_C(0x80000001)) {
        return false;
    }
    uint32_t feature_a, feature_b, feature_c, feature_d;
    __asm__ volatile ("cpuid"
                      : "=a"(feature_a), "=b"(feature_b),
                        "=c"(feature_c), "=d"(feature_d)
                      : "a"(UINT32_C(0x80000001)), "c"(0));
    (void)feature_a;
    (void)feature_b;
    (void)feature_c;
    if ((feature_d & X86_CPUID_NX) == 0) {
        return false;
    }

    uint32_t efer_low, efer_high;
    __asm__ volatile ("rdmsr"
                      : "=a"(efer_low), "=d"(efer_high)
                      : "c"(X86_EFER_MSR));
    uint64_t efer = ((uint64_t)efer_high << 32) | efer_low;
    efer |= X86_EFER_NXE;
    efer_low = (uint32_t)efer;
    efer_high = (uint32_t)(efer >> 32);
    __asm__ volatile ("wrmsr"
                      : : "c"(X86_EFER_MSR), "a"(efer_low), "d"(efer_high)
                      : "memory");
    return true;
}

bool nyr_vmm_translate_from_root(uint64_t root_physical,
                                 uint64_t hhdm_offset,
                                 uint64_t virtual_address,
                                 struct nyr_vmm_translation *translation) {
    if (translation == NULL || !canonical_48(virtual_address) ||
        (root_physical & NYR_PAGE_MASK) != 0 ||
        (root_physical & ~X86_PTE_ADDRESS) != 0) {
        return false;
    }

    volatile uint64_t *pml4 = table_pointer(root_physical, hhdm_offset);
    if (pml4 == NULL) {
        return false;
    }

    bool writable = true;
    bool user_accessible = true;
    bool executable = true;
    uint64_t entry;

    const uint16_t pml4_index = (uint16_t)((virtual_address >> 39) & 0x1ffu);
    if (!read_present_entry(pml4, pml4_index, &entry) ||
        (entry & X86_PTE_HUGE) != 0) {
        return false;
    }
    writable = writable && ((entry & X86_PTE_WRITABLE) != 0);
    user_accessible = user_accessible && ((entry & X86_PTE_USER) != 0);
    executable = executable && ((entry & X86_PTE_NO_EXECUTE) == 0);

    volatile uint64_t *pdpt = table_pointer(entry & X86_PTE_ADDRESS, hhdm_offset);
    if (pdpt == NULL) return false;

    const uint16_t pdpt_index = (uint16_t)((virtual_address >> 30) & 0x1ffu);
    if (!read_present_entry(pdpt, pdpt_index, &entry)) return false;
    writable = writable && ((entry & X86_PTE_WRITABLE) != 0);
    user_accessible = user_accessible && ((entry & X86_PTE_USER) != 0);
    executable = executable && ((entry & X86_PTE_NO_EXECUTE) == 0);

    if ((entry & X86_PTE_HUGE) != 0) {
        if ((entry & X86_1G_RESERVED) != 0) return false;
        translation->physical_address =
            (entry & X86_1G_ADDRESS) | (virtual_address & (X86_1G_SIZE - 1u));
        translation->page_size = X86_1G_SIZE;
        translation->writable = writable;
        translation->user_accessible = user_accessible;
        translation->executable = executable;
        return true;
    }

    volatile uint64_t *pd = table_pointer(entry & X86_PTE_ADDRESS, hhdm_offset);
    if (pd == NULL) return false;

    const uint16_t pd_index = (uint16_t)((virtual_address >> 21) & 0x1ffu);
    if (!read_present_entry(pd, pd_index, &entry)) return false;
    writable = writable && ((entry & X86_PTE_WRITABLE) != 0);
    user_accessible = user_accessible && ((entry & X86_PTE_USER) != 0);
    executable = executable && ((entry & X86_PTE_NO_EXECUTE) == 0);

    if ((entry & X86_PTE_HUGE) != 0) {
        if ((entry & X86_2M_RESERVED) != 0) return false;
        translation->physical_address =
            (entry & X86_2M_ADDRESS) | (virtual_address & (X86_2M_SIZE - 1u));
        translation->page_size = X86_2M_SIZE;
        translation->writable = writable;
        translation->user_accessible = user_accessible;
        translation->executable = executable;
        return true;
    }

    volatile uint64_t *pt = table_pointer(entry & X86_PTE_ADDRESS, hhdm_offset);
    if (pt == NULL) return false;

    const uint16_t pt_index = (uint16_t)((virtual_address >> 12) & 0x1ffu);
    if (!read_present_entry(pt, pt_index, &entry)) return false;
    writable = writable && ((entry & X86_PTE_WRITABLE) != 0);
    user_accessible = user_accessible && ((entry & X86_PTE_USER) != 0);
    executable = executable && ((entry & X86_PTE_NO_EXECUTE) == 0);

    translation->physical_address =
        (entry & X86_PTE_ADDRESS) | (virtual_address & NYR_PAGE_MASK);
    translation->page_size = NYR_PAGE_SIZE;
    translation->writable = writable;
    translation->user_accessible = user_accessible;
    translation->executable = executable;
    return true;
}

bool nyr_vmm_translate_current(uint64_t hhdm_offset,
                               uint64_t virtual_address,
                               struct nyr_vmm_translation *translation) {
    return nyr_vmm_translate_from_root(active_root(), hhdm_offset,
                                       virtual_address, translation);
}

bool nyr_vmm_map_page(uint64_t hhdm_offset,
                      uint64_t virtual_address,
                      uint64_t physical_address,
                      uint32_t flags,
                      enum nyr_vmm_page_ownership ownership) {
    const uint32_t allowed_flags =
        NYR_VMM_MAP_WRITE | NYR_VMM_MAP_USER | NYR_VMM_MAP_EXECUTE;
    if (!canonical_48(virtual_address) ||
        (virtual_address & NYR_PAGE_MASK) != 0 ||
        (physical_address & NYR_PAGE_MASK) != 0 ||
        (physical_address & ~X86_PTE_ADDRESS) != 0 ||
        (flags & ~allowed_flags) != 0 ||
        ((flags & NYR_VMM_MAP_WRITE) != 0 &&
         (flags & NYR_VMM_MAP_EXECUTE) != 0) ||
        (ownership != NYR_VMM_PAGE_BORROWED &&
         ownership != NYR_VMM_PAGE_OWNED) ||
        (ownership == NYR_VMM_PAGE_OWNED &&
         !nyr_pmm_frame_is_allocated(physical_address))) {
        return false;
    }

    const uint64_t irq_state = irq_save_disable();
    volatile uint64_t *table = table_pointer(active_root(), hhdm_offset);
    if (table == NULL) {
        irq_restore(irq_state);
        return false;
    }

    volatile uint64_t *created_entries[3] = { NULL, NULL, NULL };
    uint64_t created_frames[3] = { 0, 0, 0 };
    unsigned int created_count = 0;
    const uint16_t indices[3] = {
        (uint16_t)((virtual_address >> 39) & 0x1ffu),
        (uint16_t)((virtual_address >> 30) & 0x1ffu),
        (uint16_t)((virtual_address >> 21) & 0x1ffu)
    };
    bool success = true;

    for (unsigned int level = 0; level < 3u; ++level) {
        volatile uint64_t *entry_pointer = &table[indices[level]];
        uint64_t entry = *entry_pointer;
        if ((entry & X86_PTE_PRESENT) == 0) {
            uint64_t new_frame;
            volatile uint64_t *new_table;
            if (!allocate_table(hhdm_offset, &new_frame, &new_table)) {
                success = false;
                break;
            }
            *entry_pointer = new_frame | X86_PTE_PRESENT |
                             X86_PTE_WRITABLE | X86_PTE_USER |
                             X86_PTE_OWNED_TABLE;
            created_entries[created_count] = entry_pointer;
            created_frames[created_count] = new_frame;
            ++created_count;
            table = new_table;
            continue;
        }

        if ((entry & X86_PTE_HUGE) != 0 ||
            ((flags & NYR_VMM_MAP_WRITE) != 0 &&
             (entry & X86_PTE_WRITABLE) == 0) ||
            ((flags & NYR_VMM_MAP_USER) != 0 &&
             (entry & X86_PTE_USER) == 0) ||
            ((flags & NYR_VMM_MAP_EXECUTE) != 0 &&
             (entry & X86_PTE_NO_EXECUTE) != 0)) {
            success = false;
            break;
        }

        table = table_pointer(entry & X86_PTE_ADDRESS, hhdm_offset);
        if (table == NULL) {
            success = false;
            break;
        }
    }

    if (success) {
        volatile uint64_t *leaf = &table[(virtual_address >> 12) & 0x1ffu];
        if ((*leaf & X86_PTE_PRESENT) != 0) {
            success = false;
        } else {
            uint64_t value = physical_address | X86_PTE_PRESENT;
            if ((flags & NYR_VMM_MAP_WRITE) != 0) value |= X86_PTE_WRITABLE;
            if ((flags & NYR_VMM_MAP_USER) != 0) value |= X86_PTE_USER;
            if ((flags & NYR_VMM_MAP_EXECUTE) == 0) value |= X86_PTE_NO_EXECUTE;
            if (ownership == NYR_VMM_PAGE_OWNED) value |= X86_PTE_OWNED_PAGE;
            *leaf = value;
            invalidate_page(virtual_address);
        }
    }

    if (!success) {
        while (created_count != 0) {
            --created_count;
            *created_entries[created_count] = 0;
            (void)nyr_pmm_free_frame(created_frames[created_count]);
        }
    }
    irq_restore(irq_state);
    return success;
}

bool nyr_vmm_unmap_page(uint64_t hhdm_offset,
                        uint64_t virtual_address,
                        struct nyr_vmm_unmapped_page *unmapped) {
    if (unmapped == NULL || !canonical_48(virtual_address) ||
        (virtual_address & NYR_PAGE_MASK) != 0) {
        return false;
    }

    const uint64_t irq_state = irq_save_disable();
    volatile uint64_t *table = table_pointer(active_root(), hhdm_offset);
    if (table == NULL) {
        irq_restore(irq_state);
        return false;
    }

    volatile uint64_t *parent_entries[3];
    volatile uint64_t *child_tables[3];
    uint64_t child_physical[3];
    const uint16_t indices[3] = {
        (uint16_t)((virtual_address >> 39) & 0x1ffu),
        (uint16_t)((virtual_address >> 30) & 0x1ffu),
        (uint16_t)((virtual_address >> 21) & 0x1ffu)
    };
    uint64_t entry;

    for (unsigned int level = 0; level < 3u; ++level) {
        parent_entries[level] = &table[indices[level]];
        if (!read_present_entry(table, indices[level], &entry) ||
            (entry & X86_PTE_HUGE) != 0) {
            irq_restore(irq_state);
            return false;
        }
        child_physical[level] = entry & X86_PTE_ADDRESS;
        child_tables[level] = table_pointer(child_physical[level], hhdm_offset);
        if (child_tables[level] == NULL) {
            irq_restore(irq_state);
            return false;
        }
        table = child_tables[level];
    }

    volatile uint64_t *leaf = &table[(virtual_address >> 12) & 0x1ffu];
    if (!read_present_entry(table,
                            (uint16_t)((virtual_address >> 12) & 0x1ffu),
                            &entry)) {
        irq_restore(irq_state);
        return false;
    }

    unmapped->physical_address = entry & X86_PTE_ADDRESS;
    unmapped->ownership = (entry & X86_PTE_OWNED_PAGE) != 0
                              ? NYR_VMM_PAGE_OWNED
                              : NYR_VMM_PAGE_BORROWED;
    *leaf = 0;
    invalidate_page(virtual_address);

    /* Reclaim only empty tables created and tagged by this VMM. */
    for (int level = 2; level >= 0; --level) {
        const uint64_t parent = *parent_entries[level];
        if ((parent & X86_PTE_OWNED_TABLE) == 0 ||
            !table_is_empty(child_tables[level])) {
            break;
        }
        *parent_entries[level] = 0;
        (void)nyr_pmm_free_frame(child_physical[level]);
    }

    irq_restore(irq_state);
    return true;
}

bool nyr_vmm_self_test(uint64_t hhdm_offset) {
    struct nyr_vmm_translation code_translation;
    struct nyr_vmm_translation stack_translation;
    struct nyr_vmm_translation hhdm_translation;
    uint64_t stack_pointer;
    __asm__ volatile ("mov %%rsp, %0" : "=r"(stack_pointer));

    if (!nyr_vmm_translate_current(hhdm_offset,
            (uint64_t)(uintptr_t)&nyr_vmm_self_test, &code_translation) ||
        !code_translation.executable ||
        !nyr_vmm_translate_current(hhdm_offset, stack_pointer,
                                   &stack_translation) ||
        !stack_translation.writable) {
        return false;
    }

    const uint64_t frame = nyr_pmm_alloc_frame();
    if (frame == 0 || hhdm_offset > UINT64_MAX - frame) {
        if (frame != 0) (void)nyr_pmm_free_frame(frame);
        return false;
    }
    const uint64_t hhdm_virtual = hhdm_offset + frame;
    const bool translated =
        nyr_vmm_translate_current(hhdm_offset, hhdm_virtual,
                                  &hhdm_translation) &&
        hhdm_translation.physical_address == frame &&
        hhdm_translation.writable;
    const bool released = nyr_pmm_free_frame(frame);
    if (!translated || !released) return false;

    /* Exercise page-table creation, NX permissions, TLB invalidation and ownership. */
    const uint64_t test_virtual = UINT64_C(0xffffc00000000000);
    struct nyr_vmm_translation before;
    if (nyr_vmm_translate_current(hhdm_offset, test_virtual, &before)) {
        return false;
    }

    const uint64_t owned_frame = nyr_pmm_alloc_frame();
    if (owned_frame == 0 ||
        !nyr_vmm_map_page(hhdm_offset, test_virtual, owned_frame,
                          NYR_VMM_MAP_WRITE, NYR_VMM_PAGE_OWNED)) {
        if (owned_frame != 0) (void)nyr_pmm_free_frame(owned_frame);
        return false;
    }

    volatile uint64_t *mapped = (volatile uint64_t *)(uintptr_t)test_virtual;
    *mapped = UINT64_C(0x4e5952564d4d4150);
    const bool readback = *mapped == UINT64_C(0x4e5952564d4d4150);
    struct nyr_vmm_translation after;
    const bool translated_mapping =
        nyr_vmm_translate_current(hhdm_offset, test_virtual, &after) &&
        after.physical_address == owned_frame &&
        after.writable && !after.executable && !after.user_accessible;

    struct nyr_vmm_unmapped_page unmapped;
    const bool removed = nyr_vmm_unmap_page(hhdm_offset, test_virtual, &unmapped);
    const bool absent = !nyr_vmm_translate_current(hhdm_offset,
                                                    test_virtual, &before);
    const bool ownership_returned =
        removed && unmapped.physical_address == owned_frame &&
        unmapped.ownership == NYR_VMM_PAGE_OWNED;
    const bool freed = ownership_returned && nyr_pmm_free_frame(owned_frame);
    if (!readback || !translated_mapping || !removed || !absent || !freed) {
        return false;
    }

    /* A borrowed mapping must return to its caller without freeing its frame. */
    const uint64_t borrowed_frame = nyr_pmm_alloc_frame();
    if (borrowed_frame == 0 ||
        !nyr_vmm_map_page(hhdm_offset, test_virtual, borrowed_frame, 0,
                          NYR_VMM_PAGE_BORROWED)) {
        if (borrowed_frame != 0) (void)nyr_pmm_free_frame(borrowed_frame);
        return false;
    }
    const bool duplicate_rejected =
        !nyr_vmm_map_page(hhdm_offset, test_virtual, borrowed_frame, 0,
                          NYR_VMM_PAGE_BORROWED);
    const bool borrowed_removed =
        nyr_vmm_unmap_page(hhdm_offset, test_virtual, &unmapped);
    const bool borrowed_returned =
        borrowed_removed && unmapped.physical_address == borrowed_frame &&
        unmapped.ownership == NYR_VMM_PAGE_BORROWED &&
        nyr_pmm_frame_is_allocated(borrowed_frame);
    const bool borrowed_freed =
        borrowed_returned && nyr_pmm_free_frame(borrowed_frame);
    return duplicate_rejected && borrowed_removed && borrowed_returned &&
           borrowed_freed;
}
