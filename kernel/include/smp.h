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
uint32_t nyr_smp_architecture_index(uint64_t cpu_index,
                                       uint64_t bsp_index,
                                       uint64_t cpu_count);
bool nyr_smp_discover(const struct limine_mp_response *response,
                      struct nyr_smp_topology *topology);
bool nyr_smp_start_aps(struct limine_mp_response *response,
                       const struct nyr_smp_topology *topology,
                       uint64_t spin_limit);
bool nyr_smp_tlb_shootdown(uint64_t virtual_address);
/* Flush local and every online AP's non-global TLB/page-walk cache. */
bool nyr_smp_tlb_flush_all(void);
uint64_t nyr_smp_online_count(void);
uint64_t nyr_smp_failed_count(void);
#endif
