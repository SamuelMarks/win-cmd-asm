# @file ci/alpine.Dockerfile
# @brief Alpine Linux (musl) reproducible multi-ISA, multi-assembler CI container for win-cmd-asm.
FROM alpine:3.21

ENV PATH="/root/.cargo/bin:${PATH}"

RUN apk add --no-cache bash ca-certificates curl git build-base clang llvm lld nasm make dos2unix unzip tar xz jq ruby rustup && rustup-init -y --profile minimal --component clippy,llvm-tools-preview

WORKDIR /workspace
CMD ["bash", "-lc", "export WINCMD_TC_DIR=/tmp/wincmd-tc && export PATH=\"/tmp/wincmd-tc/bin:$PATH\" && make all && cargo test --workspace"]
