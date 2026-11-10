FROM mcr.microsoft.com/devcontainers/base:ubuntu-24.04

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    build-essential clang lld nasm cmake ninja-build \
    qemu-system-x86 ovmf gdb mtools dosfstools xorriso \
    rustc cargo python3 pkg-config curl wget \
    && apt-get clean && rm -rf /var/lib/apt/lists/*
