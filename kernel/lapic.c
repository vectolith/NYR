#include "include/lapic.h"
#include "include/interrupts.h"
#include "include/vmm.h"

#define IA32_APIC_BASE UINT32_C(0x1b)
#define IA32_PAT UINT32_C(0x277)
#define APIC_BASE_ENABLE (UINT64_C(1) << 11)
#define APIC_BASE_X2APIC (UINT64_C(1) << 10)
#define APIC_BASE_ADDRESS UINT64_C(0x000ffffffffff000)
#define APIC_PHYSICAL_LIMIT UINT64_C(0x0000001000000000)
#define NYR_LAPIC_VIRTUAL_BASE UINT64_C(0xfffffe0000000000)
#define NYR_LAPIC_MAX_CPUS 256u
#define CPUID_APIC (UINT32_C(1) << 9)
#define CPUID_PAT (UINT32_C(1) << 16)
#define CPUID_X2APIC (UINT32_C(1) << 21)
#define XAPIC_ID 0x020u
#define XAPIC_VERSION 0x030u
#define XAPIC_TPR 0x080u
#define XAPIC_EOI 0x0b0u
#define XAPIC_SVR 0x0f0u
#define XAPIC_LVT_TIMER 0x320u
#define XAPIC_LVT_CMCI 0x2f0u
#define XAPIC_ICR_LOW 0x300u
#define XAPIC_ICR_HIGH 0x310u
#define XAPIC_DELIVERY_PENDING (1u << 12)
#define XAPIC_LVT_MASKED (1u << 16)
#define XAPIC_SVR_ENABLE (1u << 8)
#define XAPIC_SPURIOUS_VECTOR 0xffu
#define X2APIC_MSR_BASE UINT32_C(0x800)
#define X2APIC_ID UINT32_C(0x802)
#define X2APIC_VERSION UINT32_C(0x803)
#define X2APIC_TPR UINT32_C(0x808)
#define X2APIC_EOI UINT32_C(0x80b)
#define X2APIC_SVR UINT32_C(0x80f)
#define X2APIC_ICR UINT32_C(0x830)
#define X2APIC_LVT_TIMER UINT32_C(0x832)
#define X2APIC_LVT_CMCI UINT32_C(0x82f)
#define X2APIC_LVT_THERMAL UINT32_C(0x833)
#define X2APIC_LVT_PERF UINT32_C(0x834)
#define X2APIC_LVT_LINT0 UINT32_C(0x835)
#define X2APIC_LVT_LINT1 UINT32_C(0x836)
#define X2APIC_LVT_ERROR UINT32_C(0x837)
#define APIC_LVT_COUNT 7u
#define APIC_NMI_DELIVERY (UINT32_C(4) << 8)
#define NYR_NMI_COMMAND_PING 1u
#define NYR_NMI_COMMAND_TLB_INVALIDATE 2u
#define NYR_NMI_MAILBOX_WRITING 1u
#define NYR_NMI_MAILBOX_READY 2u

static bool lapic_prepared;
static bool lapic_x2apic;
static uint64_t lapic_physical_base;
static uint64_t lapic_hhdm_offset;
static volatile uint32_t *lapic_mmio;
static uint32_t nmi_pending[NYR_LAPIC_MAX_CPUS];
static uint32_t nmi_command[NYR_LAPIC_MAX_CPUS];
static uint64_t nmi_address[NYR_LAPIC_MAX_CPUS];
static uint64_t nmi_acknowledged[NYR_LAPIC_MAX_CPUS];

static uint64_t read_msr(uint32_t msr) {
    uint32_t low, high;
    __asm__ volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}
static void write_msr(uint32_t msr, uint64_t value) {
    __asm__ volatile ("wrmsr" : : "c"(msr), "a"((uint32_t)value),
                      "d"((uint32_t)(value >> 32)) : "memory");
}
static bool cpu_has_apic(bool *has_x2apic) {
    uint32_t a, b, c, d;
    __asm__ volatile ("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                      : "a"(1u), "c"(0u));
    (void)a; (void)b;
    if (has_x2apic != 0) *has_x2apic = (c & CPUID_X2APIC) != 0;
    return (d & CPUID_APIC) != 0;
}
static uint32_t xapic_read(uint32_t offset) {
    return lapic_mmio[offset / sizeof(uint32_t)];
}
static void xapic_write(uint32_t offset, uint32_t value) {
    lapic_mmio[offset / sizeof(uint32_t)] = value;
    __asm__ volatile ("mfence" ::: "memory");
}
static uint64_t lapic_read_register(uint32_t xapic_offset, uint32_t x2apic_msr) {
    return lapic_x2apic ? read_msr(x2apic_msr) : xapic_read(xapic_offset);
}
static void lapic_write_register(uint32_t xapic_offset, uint32_t x2apic_msr,
                                 uint64_t value) {
    if (lapic_x2apic) write_msr(x2apic_msr, value);
    else xapic_write(xapic_offset, (uint32_t)value);
}
static bool set_apic_base_for_cpu(void) {
    bool has_x2apic = false;
    if (!cpu_has_apic(&has_x2apic) || (lapic_x2apic && !has_x2apic)) return false;
    if (!lapic_x2apic) {
        uint32_t a, b, c, d;
        __asm__ volatile ("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                          : "a"(1u), "c"(0u));
        (void)a; (void)b; (void)c;
        /* PWT+PCD selects PAT entry 3; require that entry to be architectural UC. */
        if ((d & CPUID_PAT) == 0 || ((read_msr(IA32_PAT) >> 24) & 0xffu) != 0u)
            return false;
    }
    uint64_t base = read_msr(IA32_APIC_BASE);
    if ((base & APIC_BASE_ADDRESS) != lapic_physical_base) return false;
    if (lapic_x2apic) base |= APIC_BASE_ENABLE | APIC_BASE_X2APIC;
    else {
        if ((base & APIC_BASE_X2APIC) != 0) return false;
        base |= APIC_BASE_ENABLE;
    }
    write_msr(IA32_APIC_BASE, base);
    return (read_msr(IA32_APIC_BASE) & (APIC_BASE_ENABLE | APIC_BASE_X2APIC)) ==
           (APIC_BASE_ENABLE | (lapic_x2apic ? APIC_BASE_X2APIC : 0));
}

bool nyr_lapic_prepare(uint64_t hhdm_offset, bool x2apic) {
    bool has_x2apic = false;
    if (lapic_prepared || !cpu_has_apic(&has_x2apic) ||
        (x2apic && !has_x2apic)) return false;
    lapic_x2apic = x2apic;
    lapic_hhdm_offset = hhdm_offset;
    uint64_t base = read_msr(IA32_APIC_BASE);
    lapic_physical_base = base & APIC_BASE_ADDRESS;
    if (lapic_physical_base == 0 ||
        lapic_physical_base >= APIC_PHYSICAL_LIMIT ||
        (lapic_physical_base & 0xfffu) != 0) return false;
    if (!lapic_x2apic) {
        struct nyr_vmm_translation existing;
        if (nyr_vmm_translate_current(hhdm_offset, NYR_LAPIC_VIRTUAL_BASE,
                                      &existing)) return false;
        if (!nyr_vmm_map_page(hhdm_offset, NYR_LAPIC_VIRTUAL_BASE,
                              lapic_physical_base,
                              NYR_VMM_MAP_WRITE | NYR_VMM_MAP_UNCACHED,
                              NYR_VMM_PAGE_BORROWED)) return false;
        lapic_mmio = (volatile uint32_t *)(uintptr_t)NYR_LAPIC_VIRTUAL_BASE;
    }
    lapic_prepared = true;
    return set_apic_base_for_cpu();
}

bool nyr_lapic_init_cpu(uint32_t expected_lapic_id) {
    if (!lapic_prepared || !set_apic_base_for_cpu()) return false;
    if (!lapic_x2apic) {
        struct nyr_vmm_translation mapping;
        if (!nyr_vmm_translate_current(lapic_hhdm_offset,
                                       NYR_LAPIC_VIRTUAL_BASE, &mapping) ||
            mapping.physical_address != lapic_physical_base ||
            mapping.page_size != 4096u || !mapping.writable ||
            mapping.user_accessible || mapping.executable || !mapping.uncached) {
            return false;
        }
    }
    const uint64_t id = lapic_read_register(XAPIC_ID, X2APIC_ID);
    const uint32_t actual_id = lapic_x2apic ? (uint32_t)id : (uint32_t)(id >> 24);
    if (actual_id != expected_lapic_id) return false;

    lapic_write_register(XAPIC_TPR, X2APIC_TPR, 0);
    const uint32_t version = (uint32_t)lapic_read_register(XAPIC_VERSION,
                                                           X2APIC_VERSION);
    const uint32_t max_lvt = (version >> 16) & 0xffu;
    const uint32_t offsets[APIC_LVT_COUNT] = {
        XAPIC_LVT_TIMER, 0x330u, 0x340u, 0x350u, 0x360u, 0x370u,
        XAPIC_LVT_CMCI
    };
    const uint32_t msrs[APIC_LVT_COUNT] = {
        X2APIC_LVT_TIMER, X2APIC_LVT_THERMAL, X2APIC_LVT_PERF,
        X2APIC_LVT_LINT0, X2APIC_LVT_LINT1, X2APIC_LVT_ERROR,
        X2APIC_LVT_CMCI
    };
    for (uint32_t i = 0; i < APIC_LVT_COUNT && i <= max_lvt; ++i) {
        const uint64_t value = lapic_read_register(offsets[i], msrs[i]);
        lapic_write_register(offsets[i], msrs[i], value | XAPIC_LVT_MASKED);
    }
    lapic_write_register(XAPIC_EOI, X2APIC_EOI, 0);
    lapic_write_register(XAPIC_SVR, X2APIC_SVR,
                         XAPIC_SVR_ENABLE | XAPIC_SPURIOUS_VECTOR);
    return (lapic_read_register(XAPIC_SVR, X2APIC_SVR) &
            (XAPIC_SVR_ENABLE | 0xffu)) ==
           (XAPIC_SVR_ENABLE | XAPIC_SPURIOUS_VECTOR);
}

static bool send_nmi_ipi(uint32_t destination_lapic_id, uint64_t spin_limit) {
    if (lapic_x2apic) {
        const uint64_t icr = ((uint64_t)destination_lapic_id << 32) |
                             APIC_NMI_DELIVERY;
        write_msr(X2APIC_ICR, icr);
        return true;
    }
    if (destination_lapic_id > 255u) return false;
    for (uint64_t spin = 0; spin < spin_limit; ++spin) {
        if ((xapic_read(XAPIC_ICR_LOW) & XAPIC_DELIVERY_PENDING) == 0) break;
        if (spin + 1u == spin_limit) return false;
        __asm__ volatile ("pause" ::: "memory");
    }
    xapic_write(XAPIC_ICR_HIGH, destination_lapic_id << 24);
    xapic_write(XAPIC_ICR_LOW, APIC_NMI_DELIVERY);
    return true;
}

static bool canonical_48(uint64_t address) {
    const uint64_t upper = address >> 48;
    const bool sign = ((address >> 47) & 1u) != 0;
    return sign ? upper == UINT64_C(0xffff) : upper == 0;
}

static bool send_nmi_command(uint32_t destination_lapic_id,
                             uint32_t destination_cpu_index,
                             uint32_t command,
                             uint64_t address,
                             uint64_t spin_limit) {
    if (!lapic_prepared || destination_cpu_index >= NYR_LAPIC_MAX_CPUS ||
        spin_limit == 0 ||
        (command == NYR_NMI_COMMAND_TLB_INVALIDATE && !canonical_48(address))) {
        return false;
    }
    const uint64_t before = __atomic_load_n(
        &nmi_acknowledged[destination_cpu_index], __ATOMIC_ACQUIRE);
    uint32_t expected = 0;
    if (!__atomic_compare_exchange_n(&nmi_pending[destination_cpu_index],
                                     &expected, NYR_NMI_MAILBOX_WRITING, false,
                                     __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        return false;
    }
    __atomic_store_n(&nmi_command[destination_cpu_index], command, __ATOMIC_RELAXED);
    __atomic_store_n(&nmi_address[destination_cpu_index], address, __ATOMIC_RELAXED);
    __atomic_store_n(&nmi_pending[destination_cpu_index],
                     NYR_NMI_MAILBOX_READY, __ATOMIC_RELEASE);
    if (!send_nmi_ipi(destination_lapic_id, spin_limit)) {
        __atomic_store_n(&nmi_pending[destination_cpu_index], 0u, __ATOMIC_RELEASE);
        return false;
    }
    for (uint64_t spin = 0; spin < spin_limit; ++spin) {
        if (__atomic_load_n(&nmi_acknowledged[destination_cpu_index],
                            __ATOMIC_ACQUIRE) != before) return true;
        __asm__ volatile ("pause" ::: "memory");
    }
    /* Keep an issued command armed: a delayed NMI must still service it. */
    return false;
}

bool nyr_lapic_send_nmi(uint32_t destination_lapic_id,
                        uint32_t destination_cpu_index,
                        uint64_t spin_limit) {
    return send_nmi_command(destination_lapic_id, destination_cpu_index,
                            NYR_NMI_COMMAND_PING, 0, spin_limit);
}

bool nyr_lapic_send_tlb_invalidate(uint32_t destination_lapic_id,
                                   uint32_t destination_cpu_index,
                                   uint64_t virtual_address,
                                   uint64_t spin_limit) {
    if ((virtual_address & 0xfffu) != 0) return false;
    return send_nmi_command(destination_lapic_id, destination_cpu_index,
                            NYR_NMI_COMMAND_TLB_INVALIDATE,
                            virtual_address, spin_limit);
}

bool nyr_lapic_handle_nmi(void) {
    const uint32_t index = nyr_cpu_local_index();
    if (index >= NYR_LAPIC_MAX_CPUS ||
        __atomic_load_n(&nmi_pending[index], __ATOMIC_ACQUIRE) !=
            NYR_NMI_MAILBOX_READY) return false;

    const uint32_t command =
        __atomic_load_n(&nmi_command[index], __ATOMIC_RELAXED);
    if (command == NYR_NMI_COMMAND_TLB_INVALIDATE) {
        const uint64_t address =
            __atomic_load_n(&nmi_address[index], __ATOMIC_RELAXED);
        if (!canonical_48(address) || (address & 0xfffu) != 0) return false;
        __asm__ volatile ("invlpg (%0)" : : "r"((uintptr_t)address) : "memory");
    } else if (command != NYR_NMI_COMMAND_PING) {
        return false;
    }

    __atomic_store_n(&nmi_pending[index], 0u, __ATOMIC_RELEASE);
    __atomic_add_fetch(&nmi_acknowledged[index], 1u, __ATOMIC_RELEASE);
    return true;
}
