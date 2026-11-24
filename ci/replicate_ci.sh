#!/usr/bin/env bash
# @file ci/replicate_ci.sh
# @brief Local GitHub Actions CI matrix replicator for win-cmd-asm (inspired by SamuelMarks/c-ci).
# @description
#   Reproduces the full GitHub Actions CI pipeline locally without pushing:
#     1. lint       - EOL policy, shell/ruby syntax, asm-lint, clippy, rustdoc
#     2. native     - Full host multi-ISA, multi-assembler build, verify & cargo test
#     3. emu-fuzz   - 4-era (1.25, 2.00, 4.00, 7.10) 8086/286 emu-cov + diff-fuzz
#     4. docker-ubu - Ubuntu 24.04 (glibc) container full build & test
#     5. docker-alp - Alpine Linux (musl) container full build & test
#     6. coverage   - Rust workspace cargo-llvm-cov summary & footprint report
#     7. dos-qemu   - Real headless QEMU MS-DOS (1.25/2.00/4.00) & FreeDOS via Bento
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

MODE="run"
SELECTED="all"
INCLUDE_DOCKER=1
INCLUDE_MSDOS=0
FUZZ_CASES="${FUZZ_CASES:-500}"

usage() {
  cat << 'EOF'
Usage: ./ci/replicate_ci.sh [OPTIONS]

Replicates the win-cmd-asm GitHub Actions CI matrix locally.

Options:
  --list              List available local CI stages
  --only <stage>      Run a single stage: lint | native | emu-fuzz | docker-ubuntu | docker-alpine | coverage | dos-qemu
  --no-docker         Skip Docker container stages (Ubuntu & Alpine)
  --msdos             Include real headless QEMU MS-DOS / FreeDOS stage (requires Bento & QEMU)
  --fuzz-cases <n>    Override differential fuzz cases per DOS era (default: 500)
  -h, --help          Show this help message
EOF
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --list) MODE="list"; shift ;;
    --only) SELECTED="$2"; shift 2 ;;
    --no-docker) INCLUDE_DOCKER=0; shift ;;
    --msdos) INCLUDE_MSDOS=1; shift ;;
    --fuzz-cases) FUZZ_CASES="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

if [ "$MODE" = "list" ]; then
  cat << 'EOF'
Available local CI stages:
  1. lint           EOL check, sh/bash/ruby syntax, asm-lint, clippy, rustdoc
  2. native         Host multi-ISA/multi-assembler lower, build, verify, cargo test & Wine smoke
  3. emu-fuzz       4-era (1.25, 2.00, 4.00, 7.10) emu-cov + ASMCONV.COM + diff-fuzz
  4. docker-ubuntu  Ubuntu 24.04 (glibc) Docker full build, verify & test
  5. docker-alpine  Alpine Linux (musl) Docker full build, verify & test
  6. coverage       Rust workspace cargo-llvm-cov + binary footprint summary
  7. dos-qemu       Opt-in real headless QEMU MS-DOS (1.25/2.00/4.00) & FreeDOS 1.3 via Bento
EOF
  exit 0
fi

run_stage_lint() {
  echo "=== [ci-local 1/7] lint ==="
  sh ./ci/check_eol.sh
  sh -n .githooks/pre-commit
  sh -n ci/check_eol.sh
  sh -n ci/run_dos_qemu_matrix.sh
  bash -n scripts/build-all.sh scripts/bootstrap-toolchains.sh ci/replicate_ci.sh
  if command -v ruby >/dev/null 2>&1; then
    ruby -c ci/dos_qemu_ci.rb
  fi
  cargo run --release --quiet -p asm-lint -- --dir asm
  cargo clippy --workspace --all-targets --all-features -- -D warnings
  RUSTDOCFLAGS="-D warnings" cargo doc --workspace --no-deps --quiet
}

run_stage_native() {
  echo "=== [ci-local 2/7] native ==="
  make package
  cargo test --workspace --quiet
  if command -v wine >/dev/null 2>&1; then
    for bin in build/nasm/x86_32/WINCMD32.EXE build/fasm/x86_64/WINCMD64.EXE build/jwasm/x86_64/WINCMD64.EXE; do
      if [ -f "$bin" ]; then
        WINEDEBUG=-all wine "$bin" /C VER
      fi
    done
  fi
}

run_stage_emu_fuzz() {
  echo "=== [ci-local 3/7] emu-fuzz (cases=$FUZZ_CASES) ==="
  for era in 1.25 2.00 4.00 7.10; do
    cargo run --release --quiet -p emu-cov -- --com build/nasm/dos16/WINCMD.COM --era "$era" --args " /C VER"
    cargo run --release --quiet -p emu-cov -- --com build/nasm/dos16/WINCMD.COM --era "$era" --args " /C SET /A X=6*7"
    if [ "$era" != "1.25" ]; then
      cargo run --release --quiet -p emu-cov -- --com build/nasm/dos16/ASMCONV.COM --era "$era" --args " /?"
    fi
    cargo run --release --quiet -p diff-fuzz -- --era "$era" --cases "$FUZZ_CASES" build/nasm/dos16/WINCMD.COM build/fasm/dos16/WINCMD.COM build/jwasm/dos16/WINCMD.COM
  done
}

run_stage_docker() {
  local name="$1"
  local dockerfile="$2"
  local tag="$3"
  echo "=== [ci-local] docker-$name ==="
  if ! command -v docker >/dev/null 2>&1 || ! docker info >/dev/null 2>&1; then
    echo "Skipping docker-$name: docker daemon not available."
    return 0
  fi
  local plat_args=()
  case "$(uname -m)" in
    arm64|aarch64) plat_args=(--platform linux/amd64) ;;
  esac
  docker build "${plat_args[@]}" -f "$dockerfile" -t "$tag" .
  docker run "${plat_args[@]}" --rm -v "$REPO_ROOT:/workspace:ro" -e TOOLCHAIN=all -e WINCMD_TC_DIR=/tmp/wincmd-tc "$tag" bash -lc 'mkdir -p /tmp/ws && cp -a /workspace/. /tmp/ws && cd /tmp/ws && rm -rf target build dist && make lint && make lower && make build TOOLCHAIN=all && make verify TOOLCHAIN=all && cargo test --workspace'
}

run_stage_coverage() {
  echo "=== [ci-local 6/7] coverage ==="
  if command -v cargo-llvm-cov >/dev/null 2>&1; then
    cargo llvm-cov --workspace --summary-only
  else
    echo "Skipping coverage: cargo-llvm-cov not installed."
  fi
}

run_stage_dos_qemu() {
  echo "=== [ci-local 7/7] dos-qemu ==="
  sh ./ci/run_dos_qemu_matrix.sh --os all --toolchain nasm --build-dir "$REPO_ROOT/build"
}

case "$SELECTED" in
  lint) run_stage_lint ;;
  native) run_stage_native ;;
  emu-fuzz) run_stage_emu_fuzz ;;
  docker-ubuntu) run_stage_docker ubuntu ci/ubuntu.Dockerfile wincmd-ci-ubuntu:latest ;;
  docker-alpine) run_stage_docker alpine ci/alpine.Dockerfile wincmd-ci-alpine:latest ;;
  coverage) run_stage_coverage ;;
  dos-qemu) run_stage_dos_qemu ;;
  all)
    run_stage_lint
    run_stage_native
    run_stage_emu_fuzz
    if [ "$INCLUDE_DOCKER" -eq 1 ]; then
      run_stage_docker ubuntu ci/ubuntu.Dockerfile wincmd-ci-ubuntu:latest
      run_stage_docker alpine ci/alpine.Dockerfile wincmd-ci-alpine:latest
    fi
    run_stage_coverage
    if [ "$INCLUDE_MSDOS" -eq 1 ]; then
      run_stage_dos_qemu
    fi
    ;;
  *)
    echo "Unknown stage: $SELECTED" >&2
    usage >&2
    exit 2
    ;;
esac

echo "=== [ci-local] Completed successfully ==="
