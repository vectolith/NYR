.section .text.entry,"ax",@progbits
.global _start
.global stack_top
.type _start, @function
.extern nyr_kernel_main

_start:
    cli
    cld
    lea stack_top(%rip), %rsp
    and $-16, %rsp
    xor %ebp, %ebp
    call nyr_kernel_main

1:
    cli
    hlt
    jmp 1b
.size _start, . - _start

.section .bss.stack,"aw",@nobits
.balign 16
stack_bottom:
.skip 65536
stack_top:

.section .note.GNU-stack,"",@progbits
