#include "include/smp.h"
#include "include/limine.h"

#define NYR_SMP_MAX_CPUS UINT64_C(256)
#define LIMINE_MP_KNOWN_FLAGS LIMINE_MP_RESPONSE_X86_64_X2APIC

bool nyr_smp_discover(const struct limine_mp_response *response,
                      struct nyr_smp_topology *topology) {
    if (response == 0 || topology == 0 || response->cpus == 0 ||
        response->cpu_count == 0 ||
        response->cpu_count > NYR_SMP_MAX_CPUS ||
        (response->flags & ~LIMINE_MP_KNOWN_FLAGS) != 0) {
        return false;
    }

    bool bsp_found = false;
    uint32_t bsp_processor_id = 0;

    for (uint64_t i = 0; i < response->cpu_count; ++i) {
        const struct limine_mp_info *cpu = response->cpus[i];
        if (cpu == 0) {
            return false;
        }

        for (uint64_t j = 0; j < i; ++j) {
            const struct limine_mp_info *previous = response->cpus[j];
            if (previous == 0 ||
                previous->processor_id == cpu->processor_id ||
                previous->lapic_id == cpu->lapic_id) {
                return false;
            }
        }

        if (cpu->lapic_id == response->bsp_lapic_id) {
            if (bsp_found) {
                return false;
            }
            bsp_found = true;
            bsp_processor_id = cpu->processor_id;
        }
    }

    if (!bsp_found) {
        return false;
    }

    topology->cpu_count = response->cpu_count;
    topology->bsp_lapic_id = response->bsp_lapic_id;
    topology->bsp_processor_id = bsp_processor_id;
    topology->x2apic =
        (response->flags & LIMINE_MP_RESPONSE_X86_64_X2APIC) != 0;
    return true;
}
