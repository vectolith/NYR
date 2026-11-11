ARG DEVCONTAINER_IMAGE=mcr.microsoft.com/devcontainers/base:ubuntu24.04@sha256:d7c468679f45a52ad3673d06656b5bf16e488990b17216a1fa295d9e1f89d724
FROM ${DEVCONTAINER_IMAGE}

COPY toolchain.lock /tmp/nyr-toolchain.lock
COPY tools/apt-install-snapshot.sh /usr/local/bin/apt-install-snapshot
RUN . /tmp/nyr-toolchain.lock \
    && printf 'APT::Snapshot "%s";\n' "$UBUNTU_SNAPSHOT" > /etc/apt/apt.conf.d/50snapshot \
    && apt-install-snapshot \
       build-essential clang lld nasm cmake ninja-build \
       qemu-system-x86 ovmf gdb mtools dosfstools xorriso \
       rustc cargo python3 pkg-config curl wget file binutils coreutils \
    && dpkg-query -W -f='${binary:Package}=${Version}\n' > /etc/nyr-toolchain-packages.txt \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*
