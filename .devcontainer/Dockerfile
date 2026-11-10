FROM mcr.microsoft.com/devcontainers/base:ubuntu-24.04

# Use the newest security updates and package revisions available in Ubuntu 24.04
# when the Codespace image is built. The distro release stays fixed for ABI stability.
RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get upgrade -y \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
       build-essential clang lld nasm cmake ninja-build \
       qemu-system-x86 ovmf gdb mtools dosfstools xorriso \
       rustc cargo python3 pkg-config curl wget file binutils coreutils \
    && dpkg-query -W -f='${binary:Package}=${Version}\n' > /etc/nyr-toolchain-packages.txt \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*
