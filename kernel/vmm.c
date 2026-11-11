#include "include/vmm.h"
#include "include/memory.h"
#include "include/lock.h"
#include "include/smp.h"
#include "include/serial.h"

#include <stddef.h>
#include <stdint.h>

#define NYR_PAGE_SIZE       UINT64_C(4096)
#define NYR_PAGE_MASK       (NYR_PAGE_SIZE - 1u)
#define X86_PTE_PRESENT     (UINT64_C(1) << 0)
#define X86_PTE_WRITABLE    (UINT64_C(1) << 1)
#define X86_PTE_USER        (UINT64_C(1) << 2)
#define X86_PTE_WRITE_THROUGH (UINT64_C(1) << 3)
#define X86_PTE_CACHE_DISABLE (UINT64_C(1) << 4)
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

static struct nyr_spinlock vmm_lock = NYR_SPINLOCK_INITIALIZER;

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

    /* Enforce supervisor write protection for read-only PTEs. */
    uint64_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= UINT64_C(1) << 16; /* CR0.WP */
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0) : "memory");
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
        translation->uncached =
            (entry & (X86_PTE_WRITE_THROUGH | X86_PTE_CACHE_DISABLE)) ==
            (X86_PTE_WRITE_THROUGH | X86_PTE_CACHE_DISABLE);
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
        translation->uncached =
            (entry & (X86_PTE_WRITE_THROUGH | X86_PTE_CACHE_DISABLE)) ==
            (X86_PTE_WRITE_THROUGH | X86_PTE_CACHE_DISABLE);
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
    translation->uncached =
        (entry & (X86_PTE_WRITE_THROUGH | X86_PTE_CACHE_DISABLE)) ==
        (X86_PTE_WRITE_THROUGH | X86_PTE_CACHE_DISABLE);
    return true;
}

bool nyr_vmm_translate_current(uint64_t hhdm_offset,
                               uint64_t virtual_address,
                               struct nyr_vmm_translation *translation) {
    const uint64_t irq_state = nyr_spin_lock_irqsave(&vmm_lock);
    const bool result = nyr_vmm_translate_from_root(
        active_root(), hhdm_offset, virtual_address, translation);
    nyr_spin_unlock_irqrestore(&vmm_lock, irq_state);
    return result;
}

static __attribute__((noreturn)) void remote_tlb_fail_stop(void) {
    nyr_serial_write("NYR: fatal remote TLB shootdown failure\r\n");
    __asm__ volatile ("cli" ::: "memory");
    for (;;) __asm__ volatile ("hlt" ::: "memory");
}

bool nyr_vmm_map_page(uint64_t hhdm_offset,
                      uint64_t virtual_address,
                      uint64_t physical_address,
                      uint32_t flags,
                      enum nyr_vmm_page_ownership ownership) {
    const uint32_t allowed_flags =
        NYR_VMM_MAP_WRITE | NYR_VMM_MAP_USER | NYR_VMM_MAP_EXECUTE |
        NYR_VMM_MAP_UNCACHED;
    if (!canonical_48(virtual_address) ||
        (virtual_address & NYR_PAGE_MASK) != 0 ||
        (physical_address & NYR_PAGE_MASK) != 0 ||
        (physical_address & ~X86_PTE_ADDRESS) != 0 ||
        (flags & ~allowed_flags) != 0 ||
        ((flags & NYR_VMM_MAP_WRITE) != 0 &&
         (flags & NYR_VMM_MAP_EXECUTE) != 0) ||
        (ownership != NYR_VMM_PAGE_BORROWED &&
         ownership != NYR_VMM_PAGE_OWNED)) {
        return false;
    }

    const uint64_t irq_state = nyr_spin_lock_irqsave(&vmm_lock);
    volatile uint64_t *table = table_pointer(active_root(), hhdm_offset);
    if (table == NULL) {
        nyr_spin_unlock_irqrestore(&vmm_lock, irq_state);
        return false;
    }

    const bool claimed = ownership == NYR_VMM_PAGE_OWNED;
    if (claimed && !nyr_pmm_claim_frame(physical_address)) {
        nyr_spin_unlock_irqrestore(&vmm_lock, irq_state);
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
            if ((flags & NYR_VMM_MAP_UNCACHED) != 0)
                value |= X86_PTE_WRITE_THROUGH | X86_PTE_CACHE_DISABLE;
            if (ownership == NYR_VMM_PAGE_OWNED) value |= X86_PTE_OWNED_PAGE;
            *leaf = value;
            invalidate_page(virtual_address);
            if (!nyr_smp_tlb_shootdown(virtual_address))
                remote_tlb_fail_stop();
        }
    }

    if (!success) {
        while (created_count != 0) {
            --created_count;
            *created_entries[created_count] = 0;
            (void)nyr_pmm_free_frame(created_frames[created_count]);
        }
    }
    if (!success && claimed) (void)nyr_pmm_release_claim(physical_address);
    nyr_spin_unlock_irqrestore(&vmm_lock, irq_state);
    return success;
}


bool nyr_vmm_map_range(uint64_t hhdm_offset,
                       uint64_t virtual_address,
                       uint64_t physical_address,
                       uint64_t page_count,
                       uint32_t flags,
                       enum nyr_vmm_page_ownership ownership) {
    if (page_count == 0 || page_count > UINT64_C(1048576) ||
        !canonical_48(virtual_address) ||
        (virtual_address & NYR_PAGE_MASK) != 0 ||
        (physical_address & NYR_PAGE_MASK) != 0 ||
        page_count - 1u > (UINT64_MAX - virtual_address) / NYR_PAGE_SIZE ||
        page_count - 1u > (UINT64_MAX - physical_address) / NYR_PAGE_SIZE ||
        !canonical_48(virtual_address + (page_count - 1u) * NYR_PAGE_SIZE) ||
        ((physical_address + (page_count - 1u) * NYR_PAGE_SIZE) &
         ~X86_PTE_ADDRESS) != 0) {
        return false;
    }

    uint64_t mapped = 0;
    for (; mapped < page_count; ++mapped) {
        if (!nyr_vmm_map_page(hhdm_offset,
                              virtual_address + mapped * NYR_PAGE_SIZE,
                              physical_address + mapped * NYR_PAGE_SIZE,
                              flags, ownership)) {
            break;
        }
    }
    if (mapped == page_count) return true;

    while (mapped != 0) {
        --mapped;
        struct nyr_vmm_unmapped_page ignored;
        (void)nyr_vmm_unmap_page(hhdm_offset,
                                 virtual_address + mapped * NYR_PAGE_SIZE,
                                 &ignored);
    }
    return false;
}

bool nyr_vmm_protect_page(uint64_t hhdm_offset,
                          uint64_t virtual_address,
                          uint32_t flags) {
    const uint32_t allowed_flags =
        NYR_VMM_MAP_WRITE | NYR_VMM_MAP_USER | NYR_VMM_MAP_EXECUTE;
    if (!canonical_48(virtual_address) ||
        (virtual_address & NYR_PAGE_MASK) != 0 ||
        (flags & ~allowed_flags) != 0 ||
        ((flags & NYR_VMM_MAP_WRITE) != 0 &&
         (flags & NYR_VMM_MAP_EXECUTE) != 0)) {
        return false;
    }

    const uint64_t irq_state = nyr_spin_lock_irqsave(&vmm_lock);
    volatile uint64_t *table = table_pointer(active_root(), hhdm_offset);
    if (table == NULL) {
        nyr_spin_unlock_irqrestore(&vmm_lock, irq_state);
        return false;
    }
    const uint16_t indices[3] = {
        (uint16_t)((virtual_address >> 39) & 0x1ffu),
        (uint16_t)((virtual_address >> 30) & 0x1ffu),
        (uint16_t)((virtual_address >> 21) & 0x1ffu)
    };
    uint64_t entry;
    for (unsigned int level = 0; level < 3u; ++level) {
        if (!read_present_entry(table, indices[level], &entry) ||
            (entry & X86_PTE_HUGE) != 0 ||
            ((flags & NYR_VMM_MAP_WRITE) != 0 &&
             (entry & X86_PTE_WRITABLE) == 0) ||
            ((flags & NYR_VMM_MAP_USER) != 0 &&
             (entry & X86_PTE_USER) == 0) ||
            ((flags & NYR_VMM_MAP_EXECUTE) != 0 &&
             (entry & X86_PTE_NO_EXECUTE) != 0)) {
            nyr_spin_unlock_irqrestore(&vmm_lock, irq_state);
            return false;
        }
        table = table_pointer(entry & X86_PTE_ADDRESS, hhdm_offset);
        if (table == NULL) {
            nyr_spin_unlock_irqrestore(&vmm_lock, irq_state);
            return false;
        }
    }

    volatile uint64_t *leaf = &table[(virtual_address >> 12) & 0x1ffu];
    if (!read_present_entry(table,
                            (uint16_t)((virtual_address >> 12) & 0x1ffu),
                            &entry)) {
        nyr_spin_unlock_irqrestore(&vmm_lock, irq_state);
        return false;
    }
    uint64_t replacement = entry &
        ~(X86_PTE_WRITABLE | X86_PTE_USER | X86_PTE_NO_EXECUTE);
    if ((flags & NYR_VMM_MAP_WRITE) != 0) replacement |= X86_PTE_WRITABLE;
    if ((flags & NYR_VMM_MAP_USER) != 0) replacement |= X86_PTE_USER;
    if ((flags & NYR_VMM_MAP_EXECUTE) == 0) replacement |= X86_PTE_NO_EXECUTE;
    *leaf = replacement;
    invalidate_page(virtual_address);
    if (!nyr_smp_tlb_shootdown(virtual_address)) remote_tlb_fail_stop();
    nyr_spin_unlock_irqrestore(&vmm_lock, irq_state);
    return true;
}

bool nyr_vmm_unmap_page(uint64_t hhdm_offset,
                        uint64_t virtual_address,
                        struct nyr_vmm_unmapped_page *unmapped) {
    if (unmapped == NULL || !canonical_48(virtual_address) ||
        (virtual_address & NYR_PAGE_MASK) != 0) {
        return false;
    }

    const uint64_t irq_state = nyr_spin_lock_irqsave(&vmm_lock);
    volatile uint64_t *table = table_pointer(active_root(), hhdm_offset);
    if (table == NULL) {
        nyr_spin_unlock_irqrestore(&vmm_lock, irq_state);
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
            nyr_spin_unlock_irqrestore(&vmm_lock, irq_state);
            return false;
        }
        child_physical[level] = entry & X86_PTE_ADDRESS;
        child_tables[level] = table_pointer(child_physical[level], hhdm_offset);
        if (child_tables[level] == NULL) {
            nyr_spin_unlock_irqrestore(&vmm_lock, irq_state);
            return false;
        }
        table = child_tables[level];
    }

    volatile uint64_t *leaf = &table[(virtual_address >> 12) & 0x1ffu];
    if (!read_present_entry(table,
                            (uint16_t)((virtual_address >> 12) & 0x1ffu),
                            &entry)) {
        nyr_spin_unlock_irqrestore(&vmm_lock, irq_state);
        return false;
    }

    unmapped->physical_address = entry & X86_PTE_ADDRESS;
    unmapped->ownership = (entry & X86_PTE_OWNED_PAGE) != 0
                              ? NYR_VMM_PAGE_OWNED
                              : NYR_VMM_PAGE_BORROWED;
    *leaf = 0;
    invalidate_page(virtual_address);
    if (!nyr_smp_tlb_shootdown(virtual_address)) remote_tlb_fail_stop();
    if (unmapped->ownership == NYR_VMM_PAGE_OWNED &&
        !nyr_pmm_release_claim(unmapped->physical_address)) {
        *leaf = entry;
        invalidate_page(virtual_address);
        if (!nyr_smp_tlb_shootdown(virtual_address)) remote_tlb_fail_stop();
        nyr_spin_unlock_irqrestore(&vmm_lock, irq_state);
        return false;
    }

    /* Detach empty VMM-owned tables, then flush every CPU before freeing them. */
    uint64_t reclaimed_frames[3];
    unsigned int reclaimed_count = 0;
    for (int level = 2; level >= 0; --level) {
        const uint64_t parent = *parent_entries[level];
        if ((parent & X86_PTE_OWNED_TABLE) == 0 ||
            !table_is_empty(child_tables[level])) break;
        *parent_entries[level] = 0;
        reclaimed_frames[reclaimed_count++] = child_physical[level];
    }
    if (reclaimed_count != 0) {
        if (!nyr_smp_tlb_flush_all()) remote_tlb_fail_stop();
        for (unsigned int i = 0; i < reclaimed_count; ++i)
            (void)nyr_pmm_free_frame(reclaimed_frames[i]);
    }

    nyr_spin_unlock_irqrestore(&vmm_lock, irq_state);
    return true;
}

bool nyr_vmm_smp_self_test(uint64_t hhdm_offset) {
    const uint64_t virtual_address = UINT64_C(0xffffd00000000000);
    struct nyr_vmm_translation translation;
    if (nyr_vmm_translate_current(hhdm_offset, virtual_address, &translation))
        return false;

    const uint64_t frame = nyr_pmm_alloc_frame();
    if (frame == 0) return false;
    if (!nyr_vmm_map_page(hhdm_offset, virtual_address, frame, 0,
                          NYR_VMM_PAGE_OWNED)) {
        (void)nyr_pmm_free_frame(frame);
        return false;
    }

    const bool mapped = nyr_vmm_translate_current(
        hhdm_offset, virtual_address, &translation) &&
        translation.physical_address == frame && !translation.writable &&
        !translation.executable && !translation.user_accessible;
    const bool protected = nyr_vmm_protect_page(
        hhdm_offset, virtual_address, NYR_VMM_MAP_WRITE);
    const bool writable = protected &&
        nyr_vmm_translate_current(hhdm_offset, virtual_address, &translation) &&
        translation.physical_address == frame && translation.writable &&
        !translation.executable;

    struct nyr_vmm_unmapped_page unmapped;
    const bool removed = nyr_vmm_unmap_page(
        hhdm_offset, virtual_address, &unmapped);
    if (!removed) return false;
    const bool absent = !nyr_vmm_translate_current(
        hhdm_offset, virtual_address, &translation);
    const bool ownership_returned =
        unmapped.physical_address == frame &&
        unmapped.ownership == NYR_VMM_PAGE_OWNED;
    const bool freed = ownership_returned && nyr_pmm_free_frame(frame);
    return mapped && writable && absent && freed;
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

    const bool protected = nyr_vmm_protect_page(
        hhdm_offset, test_virtual, NYR_VMM_MAP_EXECUTE);
    const bool protection_verified =
        protected &&
        nyr_vmm_translate_current(hhdm_offset, test_virtual, &after) &&
        !after.writable && after.executable && !after.user_accessible;
    const bool permissions_restored = nyr_vmm_protect_page(
        hhdm_offset, test_virtual, NYR_VMM_MAP_WRITE);

    struct nyr_vmm_unmapped_page unmapped;
    const bool removed = nyr_vmm_unmap_page(hhdm_offset, test_virtual, &unmapped);
    const bool absent = !nyr_vmm_translate_current(hhdm_offset,
                                                    test_virtual, &before);
    const bool ownership_returned =
        removed && unmapped.physical_address == owned_frame &&
        unmapped.ownership == NYR_VMM_PAGE_OWNED;
    const bool freed = ownership_returned && nyr_pmm_free_frame(owned_frame);
    if (!readback || !translated_mapping || !protection_verified ||
        !permissions_restored || !removed || !absent || !freed) {
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


static uint64_t vmm_read_tsc_ordered(void) {
    uint32_t low, high;
    __asm__ volatile ("lfence; rdtsc" : "=a"(low), "=d"(high) : : "memory");
    return ((uint64_t)high << 32) | low;
}

bool nyr_vmm_smp_benchmark(uint64_t hhdm_offset) {
    enum { ITERATIONS = 8 };
    const uint64_t virtual_address = UINT64_C(0xffffd00000001000);
    uint64_t map_ticks = 0, protect_ticks = 0, unmap_ticks = 0;
    uint64_t shootdown_ticks = 0, flush_ticks = 0;

    for (unsigned int i = 0; i < ITERATIONS; ++i) {
        const uint64_t frame = nyr_pmm_alloc_frame();
        if (frame == 0) return false;

        uint64_t start = vmm_read_tsc_ordered();
        const bool mapped = nyr_vmm_map_page(
            hhdm_offset, virtual_address, frame, 0, NYR_VMM_PAGE_OWNED);
        map_ticks += vmm_read_tsc_ordered() - start;
        if (!mapped) {
            (void)nyr_pmm_free_frame(frame);
            return false;
        }

        start = vmm_read_tsc_ordered();
        const bool protected = nyr_vmm_protect_page(
            hhdm_offset, virtual_address, NYR_VMM_MAP_WRITE);
        protect_ticks += vmm_read_tsc_ordered() - start;
        if (!protected) {
            struct nyr_vmm_unmapped_page cleanup;
            if (nyr_vmm_unmap_page(hhdm_offset, virtual_address, &cleanup))
                (void)nyr_pmm_free_frame(cleanup.physical_address);
            return false;
        }

        struct nyr_vmm_unmapped_page unmapped;
        start = vmm_read_tsc_ordered();
        const bool removed = nyr_vmm_unmap_page(
            hhdm_offset, virtual_address, &unmapped);
        unmap_ticks += vmm_read_tsc_ordered() - start;
        if (!removed || unmapped.physical_address != frame ||
            unmapped.ownership != NYR_VMM_PAGE_OWNED ||
            !nyr_pmm_free_frame(frame)) return false;
    }

    for (unsigned int i = 0; i < ITERATIONS; ++i) {
        uint64_t start = vmm_read_tsc_ordered();
        if (!nyr_smp_tlb_shootdown(virtual_address)) return false;
        shootdown_ticks += vmm_read_tsc_ordered() - start;

        start = vmm_read_tsc_ordered();
        if (!nyr_smp_tlb_flush_all()) return false;
        flush_ticks += vmm_read_tsc_ordered() - start;
    }

    nyr_serial_write("NYR: VMM map+shootdown average TSC ticks ");
    nyr_serial_write_u64(map_ticks / ITERATIONS);
    nyr_serial_write("\r\n");
    nyr_serial_write("NYR: VMM protect+shootdown average TSC ticks ");
    nyr_serial_write_u64(protect_ticks / ITERATIONS);
    nyr_serial_write("\r\n");
    nyr_serial_write("NYR: VMM unmap+reclaim average TSC ticks ");
    nyr_serial_write_u64(unmap_ticks / ITERATIONS);
    nyr_serial_write("\r\n");
    nyr_serial_write("NYR: remote TLB invalidate average TSC ticks ");
    nyr_serial_write_u64(shootdown_ticks / ITERATIONS);
    nyr_serial_write("\r\n");
    nyr_serial_write("NYR: remote full TLB flush average TSC ticks ");
    nyr_serial_write_u64(flush_ticks / ITERATIONS);
    nyr_serial_write("\r\n");
    return true;
}
