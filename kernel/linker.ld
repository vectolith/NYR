ENTRY(_start)
SECTIONS {
  . = 0xffffffff80000000;
  .text : ALIGN(4096) { KEEP(*(.text.entry)) *(.text*) }
  .limine_reqs : ALIGN(8) { KEEP(*(.limine_reqs)) }
  .rodata : ALIGN(4096) { *(.rodata*) }
  .data : ALIGN(4096) { *(.data*) }
  .bss : ALIGN(4096) { *(COMMON) *(.bss*) }
}
