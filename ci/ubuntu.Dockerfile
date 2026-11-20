# @file ci/ubuntu.Dockerfile
# @brief Ubuntu 24.04 (glibc) reproducible multi-ISA, multi-assembler CI container for win-cmd-asm.
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
ENV PATH="/root/.cargo/bin:${PATH}"

RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates curl git build-essential clang llvm lld nasm make dos2unix unzip tar xz-utils jq ruby mtools dosfstools qemu-system-x86 qemu-utils && rm -rf /var/lib/apt/lists/*

RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y --profile minimal --component clippy,llvm-tools-preview

WORKDIR /workspace
CMD ["bash", "-lc", "make all && cargo test --workspace"]
