#ifndef NYR_LAPIC_H
#define NYR_LAPIC_H
#include <stdbool.h>
#include <stdint.h>

/* Prepare the BSP's APIC mode and, for xAPIC, a UC MMIO mapping. */
bool nyr_lapic_prepare(uint64_t hhdm_offset, bool x2apic);
/* Enable and validate the current CPU's local APIC. */
bool nyr_lapic_init_cpu(uint32_t expected_lapic_id);
/* Send a bounded NMI IPI and wait for its lock-free per-CPU acknowledgement. */
bool nyr_lapic_send_nmi(uint32_t destination_lapic_id,
                        uint32_t destination_cpu_index,
                        uint64_t spin_limit);
/* Called by the NMI exception dispatcher; false means an unexpected NMI. */
bool nyr_lapic_handle_nmi(void);
#endif
