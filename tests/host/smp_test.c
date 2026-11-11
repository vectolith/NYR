#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../kernel/include/limine.h"
#include "../../kernel/include/smp.h"
#include "../../kernel/include/vmm.h"
#include "../../kernel/include/interrupts.h"

/*
 * The host test exercises topology validation only. AP startup is not invoked,
 * so provide a host-safe success stub for the AP callback's kernel-only NX
 * setup dependency instead of linking privileged CR/MSR instructions.
 */
bool nyr_vmm_enable_nx(void) {
    return true;
}
bool nyr_interrupts_init_cpu(uint32_t cpu_index, uint32_t lapic_id, bool bootstrap_processor) {
    (void)cpu_index; (void)lapic_id; (void)bootstrap_processor;
    return true;
}
uint32_t nyr_cpu_local_index(void) { return 0u; }
uint32_t nyr_cpu_local_lapic_id(void) { return 2u; }
bool nyr_lapic_init_cpu(uint32_t lapic_id) { (void)lapic_id; return true; }
bool nyr_lapic_send_nmi(uint32_t lapic_id, uint32_t cpu_index, uint64_t spin_limit) {
    (void)lapic_id; (void)cpu_index; (void)spin_limit; return true;
}
bool nyr_lapic_send_tlb_invalidate(uint32_t lapic_id, uint32_t cpu_index,
                                   uint64_t virtual_address, uint64_t spin_limit) {
    (void)lapic_id; (void)cpu_index; (void)virtual_address; (void)spin_limit;
    return true;
}

static int expect_rejected(const char *name,
                           const struct limine_mp_response *response) {
    struct nyr_smp_topology topology;
    if (nyr_smp_discover(response, &topology)) {
        fprintf(stderr, "host SMP test: accepted invalid %s\n", name);
        return 1;
    }
    return 0;
}

int main(void) {
    if (!nyr_smp_tlb_shootdown(UINT64_C(0xffff800000001000))) {
        fputs("host SMP test: no-AP TLB shootdown should be a no-op\n", stderr);
        return 1;
    }
    struct limine_mp_info cpu0 = {
        .processor_id = 0, .lapic_id = 2, .goto_address = 0
    };
    struct limine_mp_info cpu1 = {
        .processor_id = 1, .lapic_id = 4, .goto_address = 0
    };
    struct limine_mp_info cpu2 = {
        .processor_id = 2, .lapic_id = 6, .goto_address = 0
    };
    struct limine_mp_info cpu3 = {
        .processor_id = 3, .lapic_id = 8, .goto_address = 0
    };
    struct limine_mp_info *cpus[] = { &cpu0, &cpu1, &cpu2, &cpu3 };
    struct limine_mp_response response = {
        .revision = 0,
        .flags = 0,
        .bsp_lapic_id = 2,
        .cpu_count = 4,
        .cpus = cpus
    };
    struct nyr_smp_topology topology;

    if (!nyr_smp_discover(&response, &topology) ||
        topology.cpu_count != 4 ||
        topology.bsp_lapic_id != 2 ||
        topology.bsp_processor_id != 0 ||
        topology.x2apic) {
        fputs("host SMP test: valid four-CPU topology rejected\n", stderr);
        return 1;
    }

    response.flags = LIMINE_MP_RESPONSE_X86_64_X2APIC;
    if (!nyr_smp_discover(&response, &topology) || !topology.x2apic) {
        fputs("host SMP test: x2APIC flag not preserved\n", stderr);
        return 1;
    }
    response.flags = 0;

    /* The BSP may occupy any slot in Limine's CPU array. Its local slot is 0;
       every AP must receive one unique local slot in [1, cpu_count). */
    for (uint64_t bsp = 0; bsp < 4; ++bsp) {
        bool seen[4] = { false, false, false, false };
        for (uint64_t cpu = 0; cpu < 4; ++cpu) {
            const uint32_t local = nyr_smp_architecture_index(cpu, bsp, 4);
            if (local >= 4 || seen[local] || (cpu == bsp && local != 0) ||
                (cpu != bsp && local == 0)) {
                fputs("host SMP test: CPU-local index mapping is not unique\n", stderr);
                return 1;
            }
            seen[local] = true;
        }
        for (unsigned int local = 0; local < 4; ++local) {
            if (!seen[local]) {
                fputs("host SMP test: CPU-local index mapping has a gap\n", stderr);
                return 1;
            }
        }
    }
    if (nyr_smp_architecture_index(0, 0, 0) != UINT32_MAX ||
        nyr_smp_architecture_index(4, 0, 4) != UINT32_MAX ||
        nyr_smp_architecture_index(0, 4, 4) != UINT32_MAX) {
        fputs("host SMP test: invalid CPU-local index inputs accepted\n", stderr);
        return 1;
    }

    if (expect_rejected("null response", 0)) return 1;
    if (nyr_smp_discover(&response, 0)) {
        fputs("host SMP test: null output accepted\n", stderr);
        return 1;
    }

    struct limine_mp_response invalid = response;
    invalid.cpus = 0;
    if (expect_rejected("null CPU array", &invalid)) return 1;
    invalid = response;
    invalid.cpu_count = 0;
    if (expect_rejected("zero CPU count", &invalid)) return 1;
    invalid = response;
    invalid.cpu_count = 257;
    if (expect_rejected("oversized CPU count", &invalid)) return 1;
    invalid = response;
    invalid.flags = UINT32_C(0x80000000);
    if (expect_rejected("unknown flags", &invalid)) return 1;

    struct limine_mp_info *saved = cpus[2];
    cpus[2] = 0;
    if (expect_rejected("null CPU entry", &response)) return 1;
    cpus[2] = saved;

    const uint32_t saved_lapic = cpu3.lapic_id;
    cpu3.lapic_id = cpu2.lapic_id;
    if (expect_rejected("duplicate LAPIC ID", &response)) return 1;
    cpu3.lapic_id = saved_lapic;

    const uint32_t saved_processor = cpu3.processor_id;
    cpu3.processor_id = cpu2.processor_id;
    if (expect_rejected("duplicate processor ID", &response)) return 1;
    cpu3.processor_id = saved_processor;

    response.bsp_lapic_id = 99;
    if (expect_rejected("missing BSP", &response)) return 1;

    puts("host SMP test: CPU inventory validation passed");
    return 0;
}
