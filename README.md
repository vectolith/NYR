# NYR

NYR is a from-scratch x86-64 operating system project.

## Codespaces development

The Ubuntu 24.04 Codespaces image includes GCC/binutils, Clang/LLD, NASM, CMake/Ninja, QEMU, OVMF, GDB, FAT/ISO utilities, Rust, Python and common build utilities. It records installed package versions in `/etc/nyr-toolchain-packages.txt`. Open the repository, select `codespaces-development`, then choose **Code → Codespaces → Create codespace**. Use the browser-based VS Code terminal on iPhone.

The current development image records the resolved Ubuntu package versions but is not yet a bit-for-bit reproducible toolchain. A separately pinned toolchain baseline is a follow-up engineering task; apt upgrades are not treated as version locking.

## Build

```sh
make preflight
make clean all
make iso
```

Outputs: `build/nyr-kernel.elf` and `build/nyr-uefi.iso`. The ISO build uses Limine v12.9.6 and verifies the published SHA-256 digest of its binary release archive. It builds the Limine host-side installer from the source shipped in that archive.

## Boot

```sh
make qemu-boot-smoke
make qemu-run
```

The smoke test boots the UEFI ISO under OVMF/QEMU and requires kernel-entry and framebuffer-initialized messages on COM1. Interactive mode attaches the serial console to the Codespaces terminal.

## Current boot architecture

- Limine UEFI bootloader and the upstream Limine boot protocol header, pinned to protocol commit `6e85ddd6f928b5b512015c4d47d1cfce2fd28079`.
- Limine base-revision 6 and framebuffer requests in the protocol-defined request sections.
- Higher-half x86-64 kernel at `0xffffffff80000000`.
- Separate ELF load segments for requests, executable text, read-only data, and writable data.
- Dedicated kernel stack and COM1 diagnostics.
- 32-bpp RGB framebuffer initialization.

This is a boot milestone, not yet a complete general-purpose OS. Physical/virtual memory management, interrupt handling, scheduling, storage, drivers and userspace remain future work.

## CI

GitHub Actions builds the ELF and UEFI ISO, boots it headlessly under QEMU/OVMF, and verifies serial markers. Artifacts and logs are retained for 14 days. A successful compile or ISO build alone does not count as a successful guest boot.

## Next milestones

1. Firmware memory map and physical memory manager.
2. GDT/IDT, exceptions, APIC and timers.
3. Virtual memory manager and heap.
4. Scheduler, processes, ring 3 and syscall ABI.
5. VFS, init process, drivers, storage and userspace.
