.section .text
.code64

.global nyr_load_gdt
.type nyr_load_gdt, @function
nyr_load_gdt:
    lgdt (%rdi)
    pushq $0x08
    leaq 1f(%rip), %rax
    pushq %rax
    lretq
1:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    ret
.size nyr_load_gdt, . - nyr_load_gdt

.global nyr_load_task_register
.type nyr_load_task_register, @function
nyr_load_task_register:
    movw $0x18, %ax
    ltr %ax
    ret
.size nyr_load_task_register, . - nyr_load_task_register

.global nyr_load_idt
.type nyr_load_idt, @function
nyr_load_idt:
    lidt (%rdi)
    ret
.size nyr_load_idt, . - nyr_load_idt

.extern nyr_exception_dispatch

.macro ISR_NOERR number
.global nyr_isr\number
.type nyr_isr\number, @function
nyr_isr\number:
    pushq $0
    pushq $\number
    jmp nyr_isr_common
.endm

.macro ISR_ERROR number
.global nyr_isr\number
.type nyr_isr\number, @function
nyr_isr\number:
    pushq $\number
    jmp nyr_isr_common
.endm

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERROR 8
ISR_NOERR 9
ISR_ERROR 10
ISR_ERROR 11
ISR_ERROR 12
ISR_ERROR 13
ISR_ERROR 14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERROR 17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERROR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERROR 29
ISR_ERROR 30
ISR_NOERR 31

.global nyr_isr_common
.type nyr_isr_common, @function
nyr_isr_common:
    cld
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rbp
    pushq %rsi
    pushq %rdi
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movq %rsp, %rbx
    movq %rsp, %rdi
    andq $-16, %rsp
    call nyr_exception_dispatch
    movq %rbx, %rsp

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    addq $16, %rsp
    iretq
.size nyr_isr_common, . - nyr_isr_common

.section .rodata
.balign 8
.global nyr_exception_stub_table
nyr_exception_stub_table:
.quad nyr_isr0, nyr_isr1, nyr_isr2, nyr_isr3
.quad nyr_isr4, nyr_isr5, nyr_isr6, nyr_isr7
.quad nyr_isr8, nyr_isr9, nyr_isr10, nyr_isr11
.quad nyr_isr12, nyr_isr13, nyr_isr14, nyr_isr15
.quad nyr_isr16, nyr_isr17, nyr_isr18, nyr_isr19
.quad nyr_isr20, nyr_isr21, nyr_isr22, nyr_isr23
.quad nyr_isr24, nyr_isr25, nyr_isr26, nyr_isr27
.quad nyr_isr28, nyr_isr29, nyr_isr30, nyr_isr31

.section .note.GNU-stack,"",@progbits
