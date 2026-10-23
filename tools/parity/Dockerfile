# The development container on homebase (tools/parity/dev.sh). CI installs the same packages with apt.
FROM ubuntu:24.04
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    qemu-system-x86 qemu-utils ovmf gdisk e2fsprogs util-linux python3 zstd ca-certificates \
    && rm -rf /var/lib/apt/lists/*
