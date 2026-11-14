#ifndef NYR_TIMER_H
#define NYR_TIMER_H

#include <stdbool.h>
#include <stdint.h>

/* Legacy 8259 PIC vectors, remapped away from the CPU exception range. */
#define NYR_PIC_MASTER_BASE 0x20u
#define NYR_PIC_SLAVE_BASE 0x28u

/* Maskable timer interrupt vector used by the local APIC timer. */
#define NYR_TIMER_VECTOR 65u

/* Remap the legacy 8259 PICs and mask every line; the kernel routes IRQs
 * through the local APIC, never through the legacy controllers. */
bool nyr_timer_legacy_pic_quiesce(void);

/* Calibrate the local APIC timer against PIT channel 2, program a periodic
 * tick on NYR_TIMER_VECTOR, and prove a tick is actually delivered. */
bool nyr_timer_init(uint32_t *ticks_per_ms_out);

/* Ticks observed by the timer interrupt handler since boot. */
uint32_t nyr_timer_ticks(void);

#endif
