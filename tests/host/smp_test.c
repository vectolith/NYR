#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../kernel/include/limine.h"
#include "../../kernel/include/smp.h"

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

    if (expect_rejected("null response", 0) ||
        expect_rejected("null topology", &response)) {
        return 1;
    }
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
