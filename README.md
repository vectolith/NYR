# NYR

NYR is a from-scratch x86-64 operating system project.

## Current foundation

- Freestanding x86-64 kernel entry point and dedicated kernel stack.
- Higher-half kernel link address: `0xffffffff80000000`.
- Versioned NYR boot-information structure (`NYR0`).
- Initial 32-bpp framebuffer handoff and kernel fill test.
- Host-tool audit via `make preflight`.

## Develop from an iPhone with GitHub Codespaces

This repository includes a Codespaces development container. Open the repository in GitHub, create a Codespace from the `codespaces-development` branch, and use the browser-based VS Code terminal. No Mac, local Linux installation, or iPhone terminal app is required.

When the container is created, it installs the current build toolchain and runs the preflight and kernel build.

## Build

```sh
make preflight
make
```

Output: `build/nyr-kernel.elf`.

To rebuild from a clean state:

```sh
make clean
make
```

## Toolchain

Required for the current kernel build: a C compiler, linker, and GNU Make. The Codespaces image also installs development tools for planned work, including Clang/LLD, NASM, QEMU, OVMF, GDB, FAT/ISO utilities, CMake, Ninja, and Rust.

The preflight reports optional tools without blocking the current kernel build. It does not request root privileges or attempt package installation.

## Current limitation

The repository currently contains a kernel entry point and framebuffer test, not a complete bootable operating system. A UEFI loader and ELF-loading path are still milestones. QEMU/OVMF are installed in Codespaces for subsequent development, but a boot test is not yet available until that loader exists.

## Next milestones

1. UEFI loader and ELF loading.
2. Firmware memory map + framebuffer handoff.
3. ExitBootServices and higher-half page tables.
4. Physical/virtual memory managers.
5. GDT/IDT, exceptions, APIC/timers.
6. Scheduler, processes, ring 3, syscall ABI.
7. VFS, init process, drivers, storage and userspace.

## Mounted tools

NYR also supports a tool bundle mounted at `/mnt/tools`, or another path selected with `NYR_TOOLS_MOUNT`. Activate it with:

```sh
. tools/env.sh
```

No root access is required for the project build itself.
