#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "include/interrupts.h"
#include "include/serial.h"
#include "include/lapic.h"

#define NYR_EXCEPTION_COUNT 32u
#define NYR_MAX_CPUS 256u
#define NYR_KERNEL_CODE_SELECTOR 0x08u
#define NYR_KERNEL_DATA_SELECTOR 0x10u
#define NYR_TSS_SELECTOR 0x18u
#define NYR_KERNEL_STACK_SIZE 16384u
#define NYR_DOUBLE_FAULT_STACK_SIZE 8192u
#define IA32_GS_BASE UINT32_C(0xc0000101)

struct __attribute__((packed)) nyr_gdt_pointer {
    uint16_t limit;
    uint64_t base;
};
struct __attribute__((packed)) nyr_idt_pointer {
    uint16_t limit;
    uint64_t base;
};
struct __attribute__((packed)) nyr_idt_gate {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attributes;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
};
struct __attribute__((packed, aligned(16))) nyr_tss {
    uint32_t reserved0;
    uint64_t rsp0, rsp1, rsp2, reserved1;
    uint64_t ist1, ist2, ist3, ist4, ist5, ist6, ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
    uint8_t iomap[8193];
};
struct nyr_cpu_local {
    struct nyr_cpu_local *self;
    uint32_t index;
    uint32_t lapic_id;
    uint64_t kernel_stack_top;
    uint32_t architecture_ready;
    uint32_t reserved;
};
struct nyr_exception_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rdx, rcx, rbx, rax;
    uint64_t vector, error_code, rip, cs, rflags;
};

static uint64_t gdt[NYR_MAX_CPUS][5] __attribute__((aligned(16)));
static struct nyr_idt_gate idt[256] __attribute__((aligned(16)));
static struct nyr_tss tss[NYR_MAX_CPUS] __attribute__((aligned(16)));
static struct nyr_cpu_local cpu_local[NYR_MAX_CPUS] __attribute__((aligned(64)));
static uint8_t kernel_stacks[NYR_MAX_CPUS][NYR_KERNEL_STACK_SIZE] __attribute__((aligned(16)));
static uint8_t double_fault_stacks[NYR_MAX_CPUS][NYR_DOUBLE_FAULT_STACK_SIZE] __attribute__((aligned(16)));
static uint8_t nmi_stacks[NYR_MAX_CPUS][NYR_DOUBLE_FAULT_STACK_SIZE] __attribute__((aligned(16)));
static volatile uint64_t breakpoint_count;
static bool idt_ready;

extern char stack_top[];
extern void (*nyr_exception_stub_table[NYR_EXCEPTION_COUNT])(void);
extern void nyr_isr255(void);
extern void nyr_load_gdt(const struct nyr_gdt_pointer *pointer);
extern void nyr_load_task_register(void);
extern void nyr_load_idt(const struct nyr_idt_pointer *pointer);

static void set_tss_descriptor(uint64_t *cpu_gdt, uint64_t base, uint32_t limit) {
    cpu_gdt[3] = ((uint64_t)(limit & 0xffffu)) |
                 ((base & 0xffffffu) << 16) |
                 (UINT64_C(0x89) << 40) |
                 ((uint64_t)(limit & 0xf0000u) << 32) |
                 ((base & UINT64_C(0xff000000)) << 32);
    cpu_gdt[4] = base >> 32;
}
static void set_idt_gate(unsigned int vector, void (*handler)(void), uint8_t ist) {
    const uint64_t address = (uint64_t)(uintptr_t)handler;
    struct nyr_idt_gate *gate = &idt[vector];
    gate->offset_low = (uint16_t)address;
    gate->selector = NYR_KERNEL_CODE_SELECTOR;
    gate->ist = (uint8_t)(ist & 7u);
    gate->type_attributes = 0x8eu;
    gate->offset_middle = (uint16_t)(address >> 16);
    gate->offset_high = (uint32_t)(address >> 32);
    gate->reserved = 0;
}

void nyr_interrupts_init(void) {
    for (unsigned int i = 0; i < 256u; ++i) {
        idt[i].offset_low = 0;
        idt[i].selector = 0;
        idt[i].ist = 0;
        idt[i].type_attributes = 0;
        idt[i].offset_middle = 0;
        idt[i].offset_high = 0;
        idt[i].reserved = 0;
    }
    for (unsigned int vector = 0; vector < NYR_EXCEPTION_COUNT; ++vector) {
        set_idt_gate(vector, nyr_exception_stub_table[vector],
                     vector == 8u ? 1u : (vector == 2u ? 2u : 0u));
    }
    set_idt_gate(255u, nyr_isr255, 0u);
    idt_ready = true;
}

bool nyr_interrupts_init_cpu(uint32_t cpu_index, uint32_t lapic_id, bool bootstrap_processor) {
    if (!idt_ready || cpu_index >= NYR_MAX_CPUS ||
        __atomic_load_n(&cpu_local[cpu_index].architecture_ready, __ATOMIC_ACQUIRE) != 0u) {
        return false;
    }

    uint64_t *cpu_gdt = gdt[cpu_index];
    cpu_gdt[0] = 0;
    cpu_gdt[1] = UINT64_C(0x00af9a000000ffff);
    cpu_gdt[2] = UINT64_C(0x00cf92000000ffff);

    struct nyr_tss *cpu_tss = &tss[cpu_index];
    cpu_tss->rsp0 = bootstrap_processor
        ? (uint64_t)(uintptr_t)stack_top
        : (uint64_t)(uintptr_t)&kernel_stacks[cpu_index][NYR_KERNEL_STACK_SIZE];
    cpu_tss->ist1 = (uint64_t)(uintptr_t)
        &double_fault_stacks[cpu_index][NYR_DOUBLE_FAULT_STACK_SIZE];
    cpu_tss->ist2 = (uint64_t)(uintptr_t)
        &nmi_stacks[cpu_index][NYR_DOUBLE_FAULT_STACK_SIZE];
    cpu_tss->iomap_base = (uint16_t)offsetof(struct nyr_tss, iomap);
    for (size_t i = 0; i < sizeof(cpu_tss->iomap); ++i) cpu_tss->iomap[i] = 0xffu;
    set_tss_descriptor(cpu_gdt, (uint64_t)(uintptr_t)cpu_tss,
                       (uint32_t)sizeof(*cpu_tss) - 1u);

    struct nyr_cpu_local *local = &cpu_local[cpu_index];
    local->self = local;
    local->index = cpu_index;
    local->lapic_id = lapic_id;
    local->kernel_stack_top = cpu_tss->rsp0;
    local->reserved = 0;

    const struct nyr_gdt_pointer gdtr = {
        .limit = (uint16_t)(sizeof(gdt[cpu_index]) - 1u),
        .base = (uint64_t)(uintptr_t)cpu_gdt
    };
    const struct nyr_idt_pointer idtr = {
        .limit = (uint16_t)(sizeof(idt) - 1u),
        .base = (uint64_t)(uintptr_t)idt
    };
    /*
     * Install GS-based CPU-local state before loading our IDT. If an NMI
     * arrives after LIDT, the dispatcher must never dereference an unset GS.
     * Keep the bootloader IDT active until GDT/TR/GS are all ready.
     */
    const uint64_t gs_base = (uint64_t)(uintptr_t)local;
    __asm__ volatile ("wrmsr" : : "c"(IA32_GS_BASE),
                      "a"((uint32_t)gs_base), "d"((uint32_t)(gs_base >> 32))
                      : "memory");
    nyr_load_gdt(&gdtr);
    nyr_load_task_register();
    __atomic_store_n(&local->architecture_ready, 1u, __ATOMIC_RELEASE);
    nyr_load_idt(&idtr);
    return true;
}

uint32_t nyr_cpu_local_index(void) {
    struct nyr_cpu_local *local;
    __asm__ volatile ("movq %%gs:0, %0" : "=r"(local));
    return local->index;
}
uint32_t nyr_cpu_local_lapic_id(void) {
    struct nyr_cpu_local *local;
    __asm__ volatile ("movq %%gs:0, %0" : "=r"(local));
    return local->lapic_id;
}

__attribute__((noreturn))
static void exception_halt(void) {
    for (;;) __asm__ volatile ("cli; hlt");
}
void nyr_exception_dispatch(struct nyr_exception_frame *frame) {
    if (frame->vector == 255u) return;
    if (frame->vector == 2u && nyr_lapic_handle_nmi()) return;
    if (frame->vector == 3u) {
        __atomic_add_fetch(&breakpoint_count, 1u, __ATOMIC_RELAXED);
        return;
    }
    nyr_serial_write("NYR: fatal CPU exception vector ");
    nyr_serial_write_u64(frame->vector);
    nyr_serial_write(" error ");
    nyr_serial_write_hex(frame->error_code);
    nyr_serial_write(" RIP ");
    nyr_serial_write_hex(frame->rip);
    if (frame->vector == 14u) {
        uint64_t fault_address;
        __asm__ volatile ("mov %%cr2, %0" : "=r"(fault_address));
        nyr_serial_write(" CR2 ");
        nyr_serial_write_hex(fault_address);
    }
    nyr_serial_write("\r\n");
    exception_halt();
}
bool nyr_interrupts_self_test(void) {
    const uint64_t before = __atomic_load_n(&breakpoint_count, __ATOMIC_RELAXED);
    __asm__ volatile ("int3" : : : "memory");
    return __atomic_load_n(&breakpoint_count, __ATOMIC_RELAXED) == before + 1u;
}
