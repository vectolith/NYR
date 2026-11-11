NYR_TOOLS_MOUNT ?= /mnt/tools
RUN_WITH_TOOLS = NYR_TOOLS_MOUNT=$(NYR_TOOLS_MOUNT) ./tools/with-tools.sh
CC ?= cc
LD ?= ld
CFLAGS = -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables -fno-unwind-tables -mno-red-zone -mgeneral-regs-only -mcmodel=kernel -Wall -Wextra -Werror -O2
LDFLAGS = -nostdlib -static -no-pie -z max-page-size=0x1000 -z noexecstack --build-id=none -T kernel/linker.ld

all: build/nyr-kernel.elf

build/entry.o: kernel/entry.S
	mkdir -p build
	$(RUN_WITH_TOOLS) $(CC) -ffreestanding -m64 -mno-red-zone -c $< -o $@

build/kernel.o: kernel/kernel.c kernel/include/limine.h kernel/include/memory.h kernel/include/serial.h kernel/include/interrupts.h kernel/include/vmm.h kernel/include/smp.h
	mkdir -p build
	$(RUN_WITH_TOOLS) $(CC) $(CFLAGS) -c $< -o $@

build/memory.o: kernel/memory.c kernel/include/memory.h kernel/include/limine.h
	mkdir -p build
	$(RUN_WITH_TOOLS) $(CC) $(CFLAGS) -c $< -o $@

build/vmm.o: kernel/vmm.c kernel/include/vmm.h kernel/include/memory.h
	mkdir -p build
	$(RUN_WITH_TOOLS) $(CC) $(CFLAGS) -c $< -o $@

build/smp.o: kernel/smp.c kernel/include/smp.h kernel/include/limine.h
	mkdir -p build
	$(RUN_WITH_TOOLS) $(CC) $(CFLAGS) -c $< -o $@

build/serial.o: kernel/serial.c kernel/include/serial.h
	mkdir -p build
	$(RUN_WITH_TOOLS) $(CC) $(CFLAGS) -c $< -o $@

build/interrupts.o: kernel/interrupts.c kernel/include/interrupts.h kernel/include/serial.h
	mkdir -p build
	$(RUN_WITH_TOOLS) $(CC) $(CFLAGS) -c $< -o $@

build/interrupts_asm.o: kernel/interrupts.S
	mkdir -p build
	$(RUN_WITH_TOOLS) $(CC) -ffreestanding -m64 -mno-red-zone -c $< -o $@

build/nyr-kernel.elf: build/entry.o build/kernel.o build/memory.o build/vmm.o build/smp.o build/serial.o build/interrupts.o build/interrupts_asm.o kernel/linker.ld
	$(RUN_WITH_TOOLS) $(LD) $(LDFLAGS) build/entry.o build/kernel.o build/memory.o build/vmm.o build/smp.o build/serial.o build/interrupts.o build/interrupts_asm.o -o $@

test-host: test-pmm-host test-vmm-host test-smp-host

test-pmm-host:
	@sh tools/test-pmm-host.sh

test-vmm-host:
	@sh tools/test-vmm-host.sh

test-smp-host:
	@sh tools/test-smp-host.sh

iso: all
	@bash tools/build-iso.sh

preflight:
	@NYR_TOOLS_MOUNT=$(NYR_TOOLS_MOUNT) ./tools/preflight.sh

check-env: preflight

qemu-smoke:
	@sh tools/qemu-smoke.sh

qemu-boot-smoke: iso
	@bash tools/qemu-boot-smoke.sh

qemu-run: iso
	@bash tools/qemu-run.sh

toolchain-info:
	@cc --version | head -n 1
	@ld --version | head -n 1
	@qemu-system-x86_64 --version | head -n 1
	@if [ -f /etc/nyr-toolchain-packages.txt ]; then cat /etc/nyr-toolchain-packages.txt; fi

clean:
	rm -rf build

.PHONY: all clean check-env preflight test-host test-pmm-host test-vmm-host test-smp-host iso qemu-smoke qemu-boot-smoke qemu-run toolchain-info
