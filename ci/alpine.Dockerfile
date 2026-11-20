# @file ci/alpine.Dockerfile
# @brief Alpine Linux (musl) reproducible multi-ISA, multi-assembler CI container for win-cmd-asm.
FROM alpine:3.21

ENV PATH="/root/.cargo/bin:${PATH}"

RUN apk add --no-cache bash ca-certificates curl git build-base clang llvm lld nasm make dos2unix unzip tar xz jq ruby rust cargo

WORKDIR /workspace
CMD ["bash", "-lc", "make all && cargo test --workspace"]
