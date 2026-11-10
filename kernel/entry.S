.section .text.entry
.global _start
.extern nyr_kernel_main
_start:
    cli
    lea stack_top(%rip), %rsp
    and $-16, %rsp
    call nyr_kernel_main
1:  hlt
    jmp 1b
.section .bss
.align 16
stack_bottom:
.skip 65536
stack_top:
