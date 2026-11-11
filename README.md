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

Open the repository, select `codespaces-development`, then choose **Code → Codespaces → Create codespace**. Use the browser-based VS Code terminal on iPhone. Codespaces is configured to request a four-core machine.

## CPU and parallel build configuration

- The default development build uses up to four host CPUs. `NYR_JOBS` can override the CI job count; local Make invocations can use `make -j4`.
- Independent PMM and VMM host test binaries are separate Make targets and can run concurrently.
- CI sets four build jobs and boots QEMU with four virtual CPUs.
- QEMU uses multi-threaded TCG (`-accel tcg,thread=multi`) so guest vCPUs can execute on separate host threads when host capacity permits.
- Set `NYR_CPUS` to change the QEMU virtual CPU count; it defaults to `4`.
- Four QEMU vCPUs expose a four-CPU machine to the guest but do not mean NYR has SMP support. The kernel now validates the CPU inventory supplied by Limine's x86-64 MP protocol, including unique processor/LAPIC IDs and BSP identification. It does not yet start application processors. AP startup remains gated on per-CPU stacks and TSS state, local APIC/IPI routing, SMP-safe PMM/page-table synchronization, and remote TLB shootdowns.

## Build

```sh
make preflight
make -j4 test-host
make clean
make -j4 all iso
```

Outputs: `build/nyr-kernel.elf` and `build/nyr-uefi.iso`. The ISO builder verifies the Limine binary release digest, builds the host installer shipped as source in that archive, and creates the documented hybrid ISO layout.

## Boot

```sh
make qemu-boot-smoke
make qemu-run
```

The smoke test boots the UEFI ISO under OVMF/QEMU and requires kernel entry, interrupt self-test, framebuffer initialization, PMM self-test, VMM mapping/ownership/TLB self-tests, and the final boot milestone on COM1. CI also runs host-side PMM, VMM, and SMP CPU-inventory validation tests.

## Current boot architecture

- Limine UEFI bootloader and upstream protocol header, pinned in `toolchain.lock`.
- Limine base revision 6 and protocol-defined request marker sections.
- Higher-half x86-64 kernel at `0xffffffff80000000`.
- Separate ELF load segments for requests, executable text, read-only data, and writable data.
- Dedicated kernel stack and COM1 diagnostics.
- 32-bpp RGB framebuffer initialization.
- Explicit four-level x86-64 paging request and a page-table walker for 4 KiB, 2 MiB, and 1 GiB mappings plus active-address-space 4 KiB mapping and unmapping.
- Effective writable, user-accessible, and executable permission calculation across all page-table levels.

This is a verified early-kernel milestone, not yet a complete general-purpose OS. The current kernel still has a single-CPU physical frame allocator and basic exception/interrupt descriptor setup. CPU topology discovery is implemented; AP execution is not. The VMM supports 4 KiB mapping/unmapping, contiguous range mapping with rollback, leaf permission changes, owned/borrowed page lifetime tracking, W^X enforcement, NX enablement, CR0.WP, page-table allocation/reclamation, and local TLB invalidation. Existing huge-page mappings can be translated but are not yet split for 4 KiB edits. The PMM and page-table mutation paths remain single-CPU; remote TLB shootdowns require AP startup and IPI routing, neither of which is implemented yet. Interrupt controllers and external IRQ routing, scheduling, storage, drivers and userspace remain future work.

## CI validation

GitHub Actions builds the ELF and UEFI ISO, checks ELF load permissions and ISO structure, boots the image under four QEMU vCPUs, and verifies serial markers. The successful boot run is recorded in the pull request.

## Next milestones

1. Virtual memory mapping/unmapping with explicit page ownership, permission enforcement, and TLB invalidation.
2. Interrupt controller setup (PIC/APIC), external IRQ routing, and timer calibration.
3. Per-CPU GDT/TSS/IDT and local APIC/IPI routing; then SMP-safe PMM/page-table locks and remote TLB shootdowns.
4. Scheduler, processes, ring 3 and syscall ABI.
5. VFS, init process, drivers, storage and userspace.

## Virtual memory management

The VMM requests four-level paging from Limine and walks the active CR3 hierarchy through the HHDM. It supports individual 4 KiB mappings, contiguous range mapping with rollback, unmapping with explicit ownership return, and leaf permission changes in the active address space.

- Translates 4 KiB PTE mappings and 2 MiB/1 GiB huge-page mappings.
- Calculates effective writable, user-accessible, and executable permissions by combining flags across all paging levels.
- Rejects non-canonical virtual addresses, absent entries, unaligned roots, and reserved address bits in huge-page entries.
- Boot self-test validates kernel text, the active stack, HHDM access, owned/borrowed mapping lifetimes, and a read-only/executable permission transition.
- Host tests use synthetic page tables to verify translations, huge pages, effective permissions, and malformed mappings.

Existing 1 GiB and 2 MiB mappings are translated but not yet split for 4 KiB edits. TLB invalidation is local (`invlpg`) only. The current kernel has no AP startup or IPI path, so page-table mutation and the PMM are single-CPU only; remote TLB shootdowns are not yet available.

## Physical memory manager

The early physical page-frame allocator consumes Limine's memory-map and HHDM responses.

- Manages only complete 4 KiB pages wholly contained in `LIMINE_MEMMAP_USABLE` regions.
- Reserves the first MiB and all non-usable memory-map regions.
- Rejects malformed, overflowing, or overlapping memory-map entries.
- Stores a two-bit state per physical frame in metadata pages carved from usable RAM; those pages remain reserved.
- Provides frame allocation, release, allocation counters, and rejection of unaligned, unmanaged, and double-freed addresses.
- Runs a QEMU boot self-test covering unique aligned allocations, invalid frees, metadata protection, release, and reuse.

The allocator is deliberately single-CPU at this stage. It must gain interrupt-safe/SMP synchronization before being called concurrently or from interrupt context.
