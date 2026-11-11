# NYR

NYR is a from-scratch x86-64 operating system project.

## Reproducible development environment

The build inputs are recorded in `toolchain.lock`:

- Ubuntu 24.04 package archive snapshot: `20261111T070000Z`.
- Codespaces base image pinned by OCI digest.
- Limine v12.9.6 binary release pinned by SHA-256.
- Limine protocol header pinned to an exact upstream commit.
- GitHub Actions pinned to immutable commit SHAs.

Both Codespaces and CI use the same base image and Ubuntu package snapshot. Package versions are also recorded in `/etc/nyr-toolchain-packages.txt` in the development container and in CI logs. Updating the snapshot is an intentional toolchain change and must be followed by the complete build and boot validation.

Open the repository, select `codespaces-development`, then choose **Code → Codespaces → Create codespace**. Use the browser-based VS Code terminal on iPhone.

## Build

```sh
make preflight
make clean all
make iso
```

Outputs: `build/nyr-kernel.elf` and `build/nyr-uefi.iso`. The ISO builder verifies the Limine binary release digest, builds the host installer shipped as source in that archive, and creates the documented hybrid ISO layout.

## Boot

```sh
make qemu-boot-smoke
make qemu-run
```

The smoke test boots the UEFI ISO under OVMF/QEMU and requires kernel-entry and framebuffer-initialized messages on COM1.

## Current boot architecture

- Limine UEFI bootloader and upstream protocol header, pinned in `toolchain.lock`.
- Limine base revision 6 and protocol-defined request marker sections.
- Higher-half x86-64 kernel at `0xffffffff80000000`.
- Separate ELF load segments for requests, executable text, read-only data, and writable data.
- Dedicated kernel stack and COM1 diagnostics.
- 32-bpp RGB framebuffer initialization.

This is a verified boot milestone, not yet a complete general-purpose OS. Physical/virtual memory management, interrupt handling, scheduling, storage, drivers and userspace remain future work.

## CI validation

GitHub Actions builds the ELF and UEFI ISO, checks ELF load permissions and ISO structure, boots the image under QEMU/OVMF, and verifies serial markers. The successful boot run is recorded in the pull request.

## Next milestones

1. Firmware memory map and physical memory manager.
2. GDT/IDT, exceptions, APIC and timers.
3. Virtual memory manager and heap.
4. Scheduler, processes, ring 3 and syscall ABI.
5. VFS, init process, drivers, storage and userspace.


## Physical memory manager

The early physical page-frame allocator consumes Limine's memory-map and HHDM responses.

- Manages only complete 4 KiB pages wholly contained in `LIMINE_MEMMAP_USABLE` regions.
- Reserves the first MiB and all non-usable memory-map regions.
- Rejects malformed, overflowing, or overlapping memory-map entries.
- Stores a two-bit state per physical frame in metadata pages carved from usable RAM; those pages remain reserved.
- Provides frame allocation, release, allocation counters, and rejection of unaligned, unmanaged, and double-freed addresses.
- Runs a QEMU boot self-test covering unique aligned allocations, invalid frees, metadata protection, release, and reuse.

The allocator is deliberately single-CPU at this stage. It must gain interrupt-safe/SMP synchronization before being called concurrently or from interrupt context. It returns physical addresses; it does not yet create or modify page-table mappings.
