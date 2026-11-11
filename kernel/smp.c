#include "include/smp.h"
#include "include/limine.h"
#include "include/vmm.h"
#include "include/interrupts.h"
#include "include/lapic.h"
#define NYR_SMP_MAX_CPUS UINT64_C(256)
#define LIMINE_MP_KNOWN_FLAGS LIMINE_MP_RESPONSE_X86_64_X2APIC

struct nyr_smp_cpu_state {
    uint32_t processor_id;
    uint32_t lapic_id;
    uint32_t online;
    uint32_t architecture_index;
};
uint32_t nyr_smp_architecture_index(uint64_t cpu_index,
                                       uint64_t bsp_index,
                                       uint64_t cpu_count) {
    if (cpu_count == 0 || cpu_count > NYR_SMP_MAX_CPUS ||
        cpu_index >= cpu_count || bsp_index >= cpu_count) return UINT32_MAX;
    if (cpu_index == bsp_index) return 0u;
    return cpu_index < bsp_index ? (uint32_t)(cpu_index + 1u)
                                 : (uint32_t)cpu_index;
}

static struct nyr_smp_cpu_state cpu_state[NYR_SMP_MAX_CPUS];
static uint64_t configured_cpu_count;
static uint64_t online_cpus;
static uint64_t failed_cpus;

static __attribute__((noreturn)) void ap_park_forever(void) {
    __asm__ volatile ("cli" ::: "memory");
    for (;;) __asm__ volatile ("hlt" ::: "memory");
}

static void nyr_smp_ap_entry(struct limine_mp_info *info) {
    __asm__ volatile ("cli" ::: "memory");
    if (info == 0) ap_park_forever();
    const uint64_t index = info->extra_argument;
    if (index >= __atomic_load_n(&configured_cpu_count, __ATOMIC_ACQUIRE)) {
        __atomic_add_fetch(&failed_cpus, 1, __ATOMIC_RELEASE);
        ap_park_forever();
    }
    struct nyr_smp_cpu_state *state = &cpu_state[index];
    if (state->processor_id != info->processor_id ||
        state->lapic_id != info->lapic_id || !nyr_vmm_enable_nx() ||
        !nyr_interrupts_init_cpu(state->architecture_index, info->lapic_id, false) ||
        !nyr_lapic_init_cpu(info->lapic_id) ||
        nyr_cpu_local_index() != state->architecture_index ||
        nyr_cpu_local_lapic_id() != info->lapic_id) {
        __atomic_add_fetch(&failed_cpus, 1, __ATOMIC_RELEASE);
        ap_park_forever();
    }
    __atomic_store_n(&state->online, 1u, __ATOMIC_RELEASE);
    __atomic_add_fetch(&online_cpus, 1u, __ATOMIC_RELEASE);
    /* APs service only explicit NMIs; maskable interrupts and scheduling stay disabled. */
    ap_park_forever();
}

bool nyr_smp_discover(const struct limine_mp_response *response,
                      struct nyr_smp_topology *topology) {
    if (response == 0 || topology == 0 || response->cpus == 0 ||
        response->cpu_count == 0 || response->cpu_count > NYR_SMP_MAX_CPUS ||
        (response->flags & ~LIMINE_MP_KNOWN_FLAGS) != 0) return false;
    bool bsp_found = false;
    uint32_t bsp_processor_id = 0;
    for (uint64_t i = 0; i < response->cpu_count; ++i) {
        const struct limine_mp_info *cpu = response->cpus[i];
        if (cpu == 0) return false;
        for (uint64_t j = 0; j < i; ++j) {
            const struct limine_mp_info *previous = response->cpus[j];
            if (previous == 0 || previous->processor_id == cpu->processor_id ||
                previous->lapic_id == cpu->lapic_id) return false;
        }
        if (cpu->lapic_id == response->bsp_lapic_id) {
            if (bsp_found) return false;
            bsp_found = true;
            bsp_processor_id = cpu->processor_id;
        }
    }
    if (!bsp_found) return false;
    topology->cpu_count = response->cpu_count;
    topology->bsp_lapic_id = response->bsp_lapic_id;
    topology->bsp_processor_id = bsp_processor_id;
    topology->x2apic =
        (response->flags & LIMINE_MP_RESPONSE_X86_64_X2APIC) != 0;
    return true;
}

bool nyr_smp_start_aps(struct limine_mp_response *response,
                       const struct nyr_smp_topology *topology,
                       uint64_t spin_limit) {
    if (response == 0 || topology == 0 || response->cpus == 0 ||
        topology->cpu_count == 0 || topology->cpu_count > NYR_SMP_MAX_CPUS ||
        response->cpu_count != topology->cpu_count || spin_limit == 0 ||
        nyr_cpu_local_index() != 0u ||
        nyr_cpu_local_lapic_id() != topology->bsp_lapic_id) return false;
    uint64_t bsp_index = UINT64_MAX;
    for (uint64_t i = 0; i < topology->cpu_count; ++i) {
        struct limine_mp_info *info = response->cpus[i];
        if (info == 0) return false;
        if (info->lapic_id == topology->bsp_lapic_id) {
            if (bsp_index != UINT64_MAX) return false;
            bsp_index = i;
        } else if (__atomic_load_n(&info->goto_address, __ATOMIC_ACQUIRE) != 0) {
            return false;
        }
    }
    if (bsp_index == UINT64_MAX) return false;
    for (uint64_t i = 0; i < topology->cpu_count; ++i) {
        struct limine_mp_info *info = response->cpus[i];
        cpu_state[i].processor_id = info->processor_id;
        cpu_state[i].lapic_id = info->lapic_id;
        cpu_state[i].architecture_index = nyr_smp_architecture_index(i, bsp_index, topology->cpu_count);
        if (cpu_state[i].architecture_index == UINT32_MAX) return false;
        __atomic_store_n(&cpu_state[i].online, 0u, __ATOMIC_RELAXED);
    }
    __atomic_store_n(&configured_cpu_count, topology->cpu_count, __ATOMIC_RELEASE);
    __atomic_store_n(&online_cpus, 1u, __ATOMIC_RELAXED);
    __atomic_store_n(&failed_cpus, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&cpu_state[bsp_index].online, 1u, __ATOMIC_RELEASE);
    for (uint64_t i = 0; i < topology->cpu_count; ++i) {
        if (i == bsp_index) continue;
        struct limine_mp_info *info = response->cpus[i];
        info->extra_argument = i;
        /* Limine specifies release publication and acquire observation. */
        __atomic_store_n(&info->goto_address, nyr_smp_ap_entry, __ATOMIC_RELEASE);
    }
    for (uint64_t spin = 0; spin < spin_limit; ++spin) {
        if (__atomic_load_n(&online_cpus, __ATOMIC_ACQUIRE) == topology->cpu_count) {
            if (__atomic_load_n(&failed_cpus, __ATOMIC_ACQUIRE) != 0) return false;
            for (uint64_t i = 0; i < topology->cpu_count; ++i) {
                if (i == bsp_index) continue;
                const struct nyr_smp_cpu_state *state = &cpu_state[i];
                if (!nyr_lapic_send_nmi(state->lapic_id,
                                        state->architecture_index, spin_limit)) {
                    __atomic_add_fetch(&failed_cpus, 1u, __ATOMIC_RELEASE);
                    return false;
                }
            }
            return true;
        }
        __asm__ volatile ("pause" ::: "memory");
    }
    return false;
}
bool nyr_smp_tlb_shootdown(uint64_t virtual_address) {
    const uint64_t count = __atomic_load_n(&configured_cpu_count, __ATOMIC_ACQUIRE);
    if (count == 0) return true;
    if (count > NYR_SMP_MAX_CPUS ||
        __atomic_load_n(&online_cpus, __ATOMIC_ACQUIRE) != count ||
        __atomic_load_n(&failed_cpus, __ATOMIC_ACQUIRE) != 0) return false;

    for (uint64_t i = 0; i < count; ++i) {
        const struct nyr_smp_cpu_state *state = &cpu_state[i];
        if (state->architecture_index == 0u) continue;
        if (__atomic_load_n(&state->online, __ATOMIC_ACQUIRE) != 1u ||
            !nyr_lapic_send_tlb_invalidate(state->lapic_id,
                                          state->architecture_index,
                                          virtual_address,
                                          UINT64_C(50000000))) {
            __atomic_add_fetch(&failed_cpus, 1u, __ATOMIC_RELEASE);
            return false;
        }
    }
    return true;
}

bool nyr_smp_tlb_flush_all(void) {
    const uint64_t count = __atomic_load_n(&configured_cpu_count, __ATOMIC_ACQUIRE);
    if (count > NYR_SMP_MAX_CPUS ||
        (count != 0 &&
         (__atomic_load_n(&online_cpus, __ATOMIC_ACQUIRE) != count ||
          __atomic_load_n(&failed_cpus, __ATOMIC_ACQUIRE) != 0))) return false;

    for (uint64_t i = 0; i < count; ++i) {
        const struct nyr_smp_cpu_state *state = &cpu_state[i];
        if (state->architecture_index == 0u) continue;
        if (__atomic_load_n(&state->online, __ATOMIC_ACQUIRE) != 1u ||
            !nyr_lapic_send_tlb_flush_all(state->lapic_id,
                                          state->architecture_index,
                                          UINT64_C(50000000))) {
            __atomic_add_fetch(&failed_cpus, 1u, __ATOMIC_RELEASE);
            return false;
        }
    }

    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0; mov %0, %%cr3"
                      : "=&r"(cr3) : : "memory");
    return true;
}

uint64_t nyr_smp_online_count(void) {
    return __atomic_load_n(&online_cpus, __ATOMIC_ACQUIRE);
}
uint64_t nyr_smp_failed_count(void) {
    return __atomic_load_n(&failed_cpus, __ATOMIC_ACQUIRE);
}
