# NYR

NYR is a from-scratch x86-64 operating system project.

## Develop from an iPhone with GitHub Codespaces

The `.devcontainer` installs the current Ubuntu 24.04 development toolchain, including GCC/binutils, Clang/LLD, NASM, CMake/Ninja, QEMU, OVMF, GDB, FAT/ISO utilities, Rust, Python and common build utilities. The container upgrades the base image's security packages and records installed package versions in `/etc/nyr-toolchain-packages.txt`.

Open the repository in GitHub, select the `codespaces-development` branch, then choose **Code → Codespaces → Create codespace**. The post-create command runs preflight and builds the kernel. Use the browser-based VS Code terminal on iPhone.

The tool set and Ubuntu release are consistent across Codespaces. Package patch versions are the newest available from Ubuntu 24.04 repositories when the image is built; they can advance over time. Dependabot checks the container base image and GitHub Actions weekly. This is a maintained, current toolchain rather than a bit-for-bit frozen supply-chain lockfile.

## Build and inspect

```sh
make preflight
make clean all
make toolchain-info
```

Kernel output: `build/nyr-kernel.elf`.

## QEMU testing

```sh
make qemu-smoke
make qemu-run
```

`make qemu-smoke` verifies that QEMU can start headlessly and create a serial log. `make qemu-run` launches a UEFI disk image with no graphical display and attaches COM1 to the terminal. In Codespaces, terminal output is viewable in the browser on a phone.

To save serial output to a file instead:

```sh
NYR_QEMU_SERIAL=file make qemu-run
```

To select a disk image explicitly:

```sh
NYR_DISK_IMAGE=build/nyr-uefi.img make qemu-run
```

The kernel now initializes COM1 and emits boot diagnostics. **A full NYR boot is not yet possible:** the repository still needs its UEFI loader, ELF loader, and disk-image creation target. Until those are implemented, `qemu-smoke` tests the emulator and serial plumbing, not a successful NYR guest boot. The UEFI runner is ready for that image when the loader milestone lands.

## Current kernel foundation

- Freestanding x86-64 entry point and dedicated kernel stack.
- Higher-half link address: `0xffffffff80000000`.
- Versioned NYR boot-information structure (`NYR0`).
- 32-bpp framebuffer fill test.
- COM1 serial diagnostics for headless debugging.

## Continuous integration and updates

GitHub Actions runs preflight, compiles the kernel, verifies the ELF, and performs a headless QEMU startup/serial test on pushes and pull requests. Build artifacts and the QEMU smoke log are retained for 14 days. Dependabot checks GitHub Actions and the Codespaces base image weekly.

## Next milestones

1. UEFI loader and ELF loading.
2. Firmware memory map + framebuffer handoff.
3. ExitBootServices and higher-half page tables.
4. Physical/virtual memory managers.
5. GDT/IDT, exceptions, APIC/timers.
6. Scheduler, processes, ring 3, syscall ABI.
7. VFS, init process, drivers, storage and userspace.
