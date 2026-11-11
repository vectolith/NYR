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
#define X86_PTE_NO_EXECUTE  (UINT64_C(1) << 63)
#define X86_PTE_ADDRESS     UINT64_C(0x000ffffffffff000)
#define X86_1G_ADDRESS     UINT64_C(0x000fffffc0000000)
#define X86_2M_ADDRESS     UINT64_C(0x000fffffffe00000)
#define X86_1G_SIZE        (UINT64_C(1) << 30)
#define X86_2M_SIZE        (UINT64_C(1) << 21)
#define X86_1G_RESERVED    UINT64_C(0x000000003fffe000)
#define X86_2M_RESERVED    UINT64_C(0x00000000001fe000)

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

    volatile uint64_t *pdpt =
        table_pointer(entry & X86_PTE_ADDRESS, hhdm_offset);
    if (pdpt == NULL) {
        return false;
    }

    const uint16_t pdpt_index =
        (uint16_t)((virtual_address >> 30) & 0x1ffu);
    if (!read_present_entry(pdpt, pdpt_index, &entry)) {
        return false;
    }
    writable = writable && ((entry & X86_PTE_WRITABLE) != 0);
    user_accessible = user_accessible && ((entry & X86_PTE_USER) != 0);
    executable = executable && ((entry & X86_PTE_NO_EXECUTE) == 0);

    if ((entry & X86_PTE_HUGE) != 0) {
        if ((entry & X86_1G_RESERVED) != 0) {
            return false;
        }
        translation->physical_address =
            (entry & X86_1G_ADDRESS) | (virtual_address & (X86_1G_SIZE - 1u));
        translation->page_size = X86_1G_SIZE;
        translation->writable = writable;
        translation->user_accessible = user_accessible;
        translation->executable = executable;
        return true;
    }

    volatile uint64_t *pd =
        table_pointer(entry & X86_PTE_ADDRESS, hhdm_offset);
    if (pd == NULL) {
        return false;
    }

    const uint16_t pd_index =
        (uint16_t)((virtual_address >> 21) & 0x1ffu);
    if (!read_present_entry(pd, pd_index, &entry)) {
        return false;
    }
    writable = writable && ((entry & X86_PTE_WRITABLE) != 0);
    user_accessible = user_accessible && ((entry & X86_PTE_USER) != 0);
    executable = executable && ((entry & X86_PTE_NO_EXECUTE) == 0);

    if ((entry & X86_PTE_HUGE) != 0) {
        if ((entry & X86_2M_RESERVED) != 0) {
            return false;
        }
        translation->physical_address =
            (entry & X86_2M_ADDRESS) | (virtual_address & (X86_2M_SIZE - 1u));
        translation->page_size = X86_2M_SIZE;
        translation->writable = writable;
        translation->user_accessible = user_accessible;
        translation->executable = executable;
        return true;
    }

    volatile uint64_t *pt =
        table_pointer(entry & X86_PTE_ADDRESS, hhdm_offset);
    if (pt == NULL) {
        return false;
    }

    const uint16_t pt_index =
        (uint16_t)((virtual_address >> 12) & 0x1ffu);
    if (!read_present_entry(pt, pt_index, &entry)) {
        return false;
    }
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
    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    return nyr_vmm_translate_from_root(cr3 & X86_PTE_ADDRESS,
                                       hhdm_offset,
                                       virtual_address,
                                       translation);
}

bool nyr_vmm_self_test(uint64_t hhdm_offset) {
    struct nyr_vmm_translation code_translation;
    struct nyr_vmm_translation stack_translation;
    struct nyr_vmm_translation hhdm_translation;
    uint64_t stack_pointer;
    __asm__ volatile ("mov %%rsp, %0" : "=r"(stack_pointer));

    if (!nyr_vmm_translate_current(
            hhdm_offset,
            (uint64_t)(uintptr_t)&nyr_vmm_self_test,
            &code_translation) ||
        !code_translation.executable ||
        !nyr_vmm_translate_current(hhdm_offset,
                                   stack_pointer,
                                   &stack_translation) ||
        !stack_translation.writable) {
        return false;
    }

    const uint64_t frame = nyr_pmm_alloc_frame();
    if (frame == 0 || hhdm_offset > UINT64_MAX - frame) {
        if (frame != 0) {
            (void)nyr_pmm_free_frame(frame);
        }
        return false;
    }

    const uint64_t hhdm_virtual = hhdm_offset + frame;
    const bool translated =
        nyr_vmm_translate_current(hhdm_offset,
                                  hhdm_virtual,
                                  &hhdm_translation) &&
        hhdm_translation.physical_address == frame &&
        hhdm_translation.writable;

    const bool released = nyr_pmm_free_frame(frame);
    return translated && released;
}
