#ifndef NYR_SMP_H
#define NYR_SMP_H

#include <stdbool.h>
#include <stdint.h>

struct limine_mp_response;

struct nyr_smp_topology {
    uint64_t cpu_count;
    uint32_t bsp_lapic_id;
    uint32_t bsp_processor_id;
    bool x2apic;
};

/*
 * Validate Limine's x86-64 CPU inventory. This stage discovers CPUs only;
 * it deliberately does not start application processors.
 */
bool nyr_smp_discover(const struct limine_mp_response *response,
                      struct nyr_smp_topology *topology);

#endif
