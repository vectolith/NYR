# NYR

NYR is a from-scratch x86-64 operating system project.

## Codespaces development
The Ubuntu 24.04 Codespaces image includes GCC/binutils, Clang/LLD, NASM, CMake/Ninja, QEMU, OVMF, GDB, FAT/ISO utilities, Rust, Python and common build utilities. It records installed package versions in `/etc/nyr-toolchain-packages.txt`. Open the repository, select `codespaces-development`, then choose **Code → Codespaces → Create codespace**. Use the browser-based VS Code terminal on iPhone.

Ubuntu package patch versions are the newest available when the image is built; Dependabot tracks the base image and GitHub Actions weekly. This is a maintained current toolchain, not a bit-for-bit frozen lockfile.

## Build
```sh
make preflight
make clean all
make iso
```
Outputs: `build/nyr-kernel.elf` and `build/nyr-uefi.iso`. The ISO script downloads Limine v12.9.6 and verifies the official release asset SHA-256 before use.

## Boot
```sh
make qemu-boot-smoke
make qemu-run
```
The smoke test boots the UEFI ISO under OVMF/QEMU and requires kernel-entry and framebuffer-initialized messages on COM1. Interactive mode attaches the serial console to the Codespaces terminal.

## Current boot architecture
- Limine UEFI bootloader and Limine boot protocol.
- Higher-half x86-64 kernel at `0xffffffff80000000`.
- Limine base-revision and framebuffer requests.
- Dedicated kernel stack and COM1 diagnostics.
- 32-bpp RGB framebuffer initialization.

This is a real UEFI-to-kernel boot milestone, not yet a complete general-purpose OS. Memory management, interrupts, scheduling, storage, drivers and userspace remain future work.

## CI
GitHub Actions builds the ELF and UEFI ISO, boots it headlessly under QEMU/OVMF, and verifies serial markers. Artifacts and logs are retained for 14 days.

## Next milestones
1. Firmware memory map and physical memory manager.
2. GDT/IDT, exceptions, APIC and timers.
3. Virtual memory manager and heap.
4. Scheduler, processes, ring 3 and syscall ABI.
5. VFS, init process, drivers, storage and userspace.
