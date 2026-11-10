NYR_TOOLS_MOUNT ?= /mnt/tools
RUN_WITH_TOOLS = NYR_TOOLS_MOUNT=$(NYR_TOOLS_MOUNT) ./tools/with-tools.sh
CC ?= cc
LD ?= ld
CFLAGS = -ffreestanding -fno-stack-protector -fno-pic -mno-red-zone -mcmodel=kernel -Wall -Wextra -Werror -O2
LDFLAGS = -nostdlib -z max-page-size=0x1000 -T kernel/linker.ld

all: build/nyr-kernel.elf

build/entry.o: kernel/entry.S
	mkdir -p build
	$(RUN_WITH_TOOLS) $(CC) -ffreestanding -m64 -mno-red-zone -c $< -o $@

build/kernel.o: kernel/kernel.c kernel/include/nyr.h
	mkdir -p build
	$(RUN_WITH_TOOLS) $(CC) $(CFLAGS) -c $< -o $@

build/nyr-kernel.elf: build/entry.o build/kernel.o kernel/linker.ld
	$(RUN_WITH_TOOLS) $(LD) $(LDFLAGS) build/entry.o build/kernel.o -o $@

preflight:
	@NYR_TOOLS_MOUNT=$(NYR_TOOLS_MOUNT) ./tools/preflight.sh

check-env: preflight

qemu-smoke:
	@sh tools/qemu-smoke.sh

qemu-run: all
	@sh tools/qemu-run.sh

toolchain-info:
	@cc --version | head -n 1
	@ld --version | head -n 1
	@qemu-system-x86_64 --version | head -n 1
	@if [ -f /etc/nyr-toolchain-packages.txt ]; then cat /etc/nyr-toolchain-packages.txt; fi

clean:
	rm -rf build

.PHONY: all clean check-env preflight qemu-smoke qemu-run toolchain-info
