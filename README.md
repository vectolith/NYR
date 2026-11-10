# NYR

NYR is a from-scratch x86-64 operating system project.

## Current foundation
- Freestanding x86-64 kernel entry point and ddicated kernel stack.
- Higher-half kernel link address: `0xffffffff800000000`.
- Versioned NYR boot-information structure (`NYR0`).
- Initial 32-bpp framebuffer handoff and kernel fill test.
- Host-tool audit via `make check-env`.

## Build
The current foundation builds with the host GCC/binutils toolchain:

    make preflight
    make

Output: `build/nyr-kernel.elf`

## Intended development stack
The project will grow to use UEFI + OVMF under QEMU for repeatable boot testing. Debugging/image tools are treated as planned dependencies until present on the host.

## Next milestones
1. UEFI loader and ELF loading.
2. Firmware memory map + framebuffer handoff.
3. ExitBootServices and higher-half page tables.
4. Physical/virtual memory managers.
5. GDT/IDT, exceptions, APIC/timers.
6. Scheduler, processes, ring 3, syscall ABI.
7. VFS, init process, drivers, storage and userspace.

## Preflight
Run `make preflight` before toolchain-dependent work. It checks compilers, linkers, assemblers, build systems, binary utilities, QEMU, OVMF/EDK II firmware, debuggers, FAT/ISO tooling, Rust, source/network utilities, and package-management privilege. It reports all missing capabilities in one run and exits non-zero unless the full planned environment is available.

## Git-based tool sources
NYR itself is a Git repository. `tools/bootstrap-git-tools.sh` clones or fast-forward-updates upstream source repositories for the missing development stack into `.toolchain/src` without requiring root. Cloning source is distinct from successfully building/installing each tool; prerequisites are still enforced by `make preflight`.

## Mounted tools
NYR supports a read-only or read-write tool bundle mounted at `/mnt/tools`, or another path selected with `NYR_TOOLS_MOUNT`. The activation script searches `/mnt/tools/bin` and each `/mnt/tools/*/bin`, prepends them to `PATH`, and discovers OVMF code/variable firmware images.

    NYR_TOOLS_MOUNT=/mnt/tools make preflight
    NYR_TOOLS_MOUNT=/mnt/tools make
    . tools/env.sh

No root access is required. Mounted executables must match the sandbox architecture and include their runtime libraries.
