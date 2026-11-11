#ifndef NYR_INTERRUPTS_H
#define NYR_INTERRUPTS_H
#include <stdbool.h>
#include <stdint.h>
void nyr_interrupts_init(void);
bool nyr_interrupts_init_cpu(uint32_t cpu_index, uint32_t lapic_id, bool bootstrap_processor);
uint32_t nyr_cpu_local_index(void);
uint32_t nyr_cpu_local_lapic_id(void);
bool nyr_interrupts_self_test(void);
#endif
