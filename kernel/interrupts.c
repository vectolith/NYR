#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "include/interrupts.h"
#include "include/serial.h"

#define NYR_EXCEPTION_COUNT 32u
#define NYR_KERNEL_CODE_SELECTOR 0x08u
#define NYR_KERNEL_DATA_SELECTOR 0x10u
#define NYR_TSS_SELECTOR 0x18u

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

struct __attribute__((packed)) nyr_tss {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
};

struct nyr_exception_frame {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rdx, rcx, rbx, rax;
    uint64_t vector;
    uint64_t error_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
};

static uint64_t gdt[5] __attribute__((aligned(16)));
static struct nyr_idt_gate idt[256] __attribute__((aligned(16)));
static struct nyr_tss tss __attribute__((aligned(16)));
static uint8_t double_fault_stack[16384] __attribute__((aligned(16)));
static volatile uint64_t breakpoint_count;

extern void (*nyr_exception_stub_table[NYR_EXCEPTION_COUNT])(void);
extern void nyr_load_gdt(const struct nyr_gdt_pointer *pointer);
extern void nyr_load_task_register(void);
extern void nyr_load_idt(const struct nyr_idt_pointer *pointer);

static void set_tss_descriptor(uint64_t base, uint32_t limit) {
    gdt[3] = ((uint64_t)(limit & 0xffffu)) |
             ((base & 0xffffffu) << 16) |
             (UINT64_C(0x89) << 40) |
             ((uint64_t)(limit & 0xf0000u) << 32) |
             ((base & UINT64_C(0xff000000)) << 32);
    gdt[4] = base >> 32;
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
    gdt[0] = 0;
    gdt[1] = UINT64_C(0x00af9a000000ffff);
    gdt[2] = UINT64_C(0x00cf92000000ffff);

    tss.rsp0 = (uint64_t)(uintptr_t)double_fault_stack + sizeof(double_fault_stack);
    tss.ist1 = (uint64_t)(uintptr_t)double_fault_stack + sizeof(double_fault_stack);
    tss.iomap_base = (uint16_t)sizeof(tss);
    set_tss_descriptor((uint64_t)(uintptr_t)&tss, (uint32_t)sizeof(tss) - 1u);

    const struct nyr_gdt_pointer gdtr = {
        .limit = (uint16_t)(sizeof(gdt) - 1u),
        .base = (uint64_t)(uintptr_t)gdt
    };
    nyr_load_gdt(&gdtr);
    nyr_load_task_register();

    for (unsigned int vector = 0; vector < NYR_EXCEPTION_COUNT; ++vector) {
        set_idt_gate(vector, nyr_exception_stub_table[vector], vector == 8u ? 1u : 0u);
    }

    const struct nyr_idt_pointer idtr = {
        .limit = (uint16_t)(sizeof(idt) - 1u),
        .base = (uint64_t)(uintptr_t)idt
    };
    nyr_load_idt(&idtr);
}

__attribute__((noreturn))
static void exception_halt(void) {
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

void nyr_exception_dispatch(struct nyr_exception_frame *frame) {
    if (frame->vector == 3u) {
        ++breakpoint_count;
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
    nyr_serial_write("\\r\\n");
    exception_halt();
}

bool nyr_interrupts_self_test(void) {
    const uint64_t before = breakpoint_count;
    __asm__ volatile ("int3" : : : "memory");
    return breakpoint_count == before + 1u;
}
