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
- Four QEMU vCPUs expose a four-CPU machine to the guest but do not mean NYR has SMP support. The kernel now validates the CPU inventory supplied by Limine's x86-64 MP protocol, including unique processor/LAPIC IDs and BSP identification. It starts APs through Limine's protocol-defined entry path after installing private per-CPU descriptor state and initializing local APICs. APs acknowledge NMI IPIs on dedicated IST stacks, then remain halted with maskable interrupts disabled. Maskable IPI routing, remote TLB shootdowns, and scheduler handoff remain gated.

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

This is an early-kernel milestone, not yet a complete general-purpose OS. The kernel has per-CPU descriptor state, local APIC initialization, and tested NMI IPI delivery to parked APs. APs do not yet execute scheduler work or accept maskable interrupts. The VMM supports 4 KiB mapping/unmapping, contiguous range mapping with rollback, leaf permission changes, owned/borrowed page lifetime tracking, W^X enforcement, NX enablement, CR0.WP, page-table allocation/reclamation, and local TLB invalidation. Existing huge-page mappings can be translated but are not yet split for 4 KiB edits. The PMM and page-table mutation paths use SMP spinlocks, but remote TLB shootdowns are not yet implemented. APs remain parked with interrupts disabled after their NMI acknowledgement. Interrupt controllers and external IRQ routing, scheduling, storage, drivers and userspace remain future work.

## CI validation

GitHub Actions builds the ELF and UEFI ISO, checks ELF load permissions and ISO structure, boots the image under four QEMU vCPUs, and verifies serial markers. The successful boot run is recorded in the pull request.

## Next milestones

1. Maskable IPI vectors and remote TLB shootdown protocol, including bounded acknowledgement and failure handling.
2. IOAPIC/legacy PIC routing, interrupt masking, and timer calibration.
3. SMP scheduler handoff and per-CPU run queues.
4. Processes, ring 3 and syscall ABI.
5. VFS, init process, drivers, storage and userspace.

## Virtual memory management

The VMM requests four-level paging from Limine and walks the active CR3 hierarchy through the HHDM. It supports individual 4 KiB mappings, contiguous range mapping with rollback, unmapping with explicit ownership return, and leaf permission changes in the active address space.

- Translates 4 KiB PTE mappings and 2 MiB/1 GiB huge-page mappings.
- Calculates effective writable, user-accessible, and executable permissions by combining flags across all paging levels.
- Rejects non-canonical virtual addresses, absent entries, unaligned roots, and reserved address bits in huge-page entries.
- Boot self-test validates kernel text, the active stack, HHDM access, owned/borrowed mapping lifetimes, and a read-only/executable permission transition.
- Host tests use synthetic page tables to verify translations, huge pages, effective permissions, and malformed mappings.

Existing 1 GiB and 2 MiB mappings are translated but not yet split for 4 KiB edits. TLB invalidation is local (`invlpg`) only. AP startup and NMI IPI delivery are implemented, and PMM/VMM mutation paths use SMP spinlocks. Remote TLB shootdowns are not yet available; local `invlpg` alone is insufficient once APs execute shared address-space work.

## Physical memory manager

The early physical page-frame allocator consumes Limine's memory-map and HHDM responses.

- Manages only complete 4 KiB pages wholly contained in `LIMINE_MEMMAP_USABLE` regions.
- Reserves the first MiB and all non-usable memory-map regions.
- Rejects malformed, overflowing, or overlapping memory-map entries.
- Stores a two-bit state per physical frame in metadata pages carved from usable RAM; those pages remain reserved.
- Provides frame allocation, release, allocation counters, and rejection of unaligned, unmanaged, and double-freed addresses.
- Runs a QEMU boot self-test covering unique aligned allocations, invalid frees, metadata protection, release, and reuse.

The allocator's allocation, release, claim, and accounting paths are serialized by an IRQ-saving SMP spinlock. Initialization remains a BSP-only boot phase; allocator use from interrupt context is not yet supported.


## Per-CPU architecture and SMP safety

The BSP and each AP install private x86-64 GDT/TSS state, kernel-entry stacks,
double-fault IST stacks, NMI IST stacks, and GS-based CPU-local records. The
shared IDT handles architectural exceptions, the breakpoint self-test, NMI
IPI acknowledgements, and the APIC spurious vector. The TSS I/O-permission
bitmap denies port I/O by default.

The local APIC is initialized in the mode reported by Limine. xAPIC uses a
dedicated kernel mapping with PWT+PCD leaf attributes; x2APIC uses MSRs. The
kernel validates each hardware LAPIC ID, masks supported local-vector-table
sources, and enables the APIC software interface. NMI IPIs are sent with a
bounded acknowledgement protocol and handled on per-CPU IST stacks. The BSP
self-test and QEMU boot test exercise BSP and AP NMI delivery.

The kernel spinlock primitive uses acquire/release atomics and IRQ-save
critical sections. PMM allocation, release, claims, and accounting are
serialized. Active-address-space VMM mapping, protection, unmapping, and
translation are serialized. Owned VMM mappings claim PMM frames against
concurrent release. Host tests include four-thread lock contention and CPU
local-index assignment with the BSP in every possible slot.

**AP execution remains deliberately gated.** APs complete per-CPU and local
APIC initialization, acknowledge NMI IPIs, then return to `cli; hlt`. They
do not accept maskable interrupts or execute scheduler work. Maskable IPI
delivery, remote TLB shootdowns, IOAPIC/legacy PIC routing, timer setup, and
scheduler handoff remain unimplemented. Local `invlpg` is not a substitute
for remote TLB shootdowns once APs execute shared address-space work.
