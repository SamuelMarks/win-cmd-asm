# win-cmd-asm Extensive CI Suite

Inspired by [SamuelMarks/c-ci](https://github.com/SamuelMarks/c-ci), this directory and `.github/workflows/` implement a reusable, cross-OS, multi-assembler, multi-ISA, and multi-DOS-era CI system that can be executed identically on GitHub Actions or locally.

## Workflows (`.github/workflows/`)

- **`asm-ci.yml`** — Reusable (`workflow_call`) pipeline with 6 orchestrated stages:
  1. **`static-lint-and-eol`**: Enforces CRLF/LF policy (`ci/check_eol.sh`), shell/Ruby syntax (`sh -n`, `bash -n`, `ruby -c`), `asm-lint --dir asm` (100% `@tag` docs, CFG stack balance, ABI/clobbers, ISA purity), `cargo clippy -- -D warnings`, and `cargo doc` (`-D warnings`).
  2. **`build-and-verify-matrix`**: Sparse cross-OS / cross-assembler / cross-ISA matrix:
     - **Linux x64 (`ubuntu-latest`)**: ALL 6 toolchains (`NASM`, `FASM`, `JWasm`, `FASMARM`, `LLVM-MC`, `lld-link`) across ALL 6 targets (`dos16`, `win16`, `x86_32`, `x86_64`, `arm32`, `arm64`) + Wine smoke tests + artifact upload.
     - **Linux ARM64 (`ubuntu-24.04-arm`, experimental)**: `NASM` + `JWasm` + `LLVM` across all 6 targets.
     - **macOS Apple Silicon (`macos-latest`)**: `NASM` + `JWasm` + Homebrew `LLVM` across all 6 targets.
     - **Windows 2025 x64 (`windows-2025`)**: WerFault suppression, native Windows `NASM`, `FASM`, `JWasm`, `FASMARM`, `LLVM`, 3-way equivalence verification, and **real Windows NT kernel execution** of `WINCMD32.EXE` & `WINCMD64.EXE`.
     - **Windows 2022 x64 (`windows-2022`)**: Native `BUILD.CMD` (`NASM`, `FASM`, `JWasm`) + real NT kernel smoke tests.
     - **Windows Cygwin (`windows-2025` + `cygwin`)**: POSIX-on-Windows (`SHELLOPTS=igncr`) `NASM` build & budget verification.
     - **Windows 11 ARM64 (`windows-11-arm`, experimental)**: Native `WINCMD_ARM64.EXE` build & execution on real ARM64 hardware.
  3. **`dos-emu-and-fuzz-matrix`**: Parallel 4-era matrix (`1.25`, `2.00`, `4.00`, `7.10`) running `emu-cov` (with per-era LCOV), self-hosted `ASMCONV.COM` smoke tests, and `diff-fuzz` (`NASM == FASM == JWasm`).
  4. **`docker-container-matrix`**: Parallel containerized verification on **Ubuntu 24.04 (glibc)** (`ci/ubuntu.Dockerfile`) and **Alpine Linux (musl)** (`ci/alpine.Dockerfile`).
  5. **`coverage-and-summary`**: `cargo llvm-cov --workspace` LCOV + GitHub Actions Step Summary with doc coverage and full binary footprint table.
  6. **`msdos-qemu-test` (opt-in via `msdos_qemu_testing: true`)**: Boots real headless QEMU **MS-DOS 1.25**, **MS-DOS 2.00**, **MS-DOS 4.00**, and **FreeDOS 1.3** guests built via `SamuelMarks/bento`, uploads `WINCMD.COM`, `WINCMD.MSG`, and `ASMCONV.COM` over COM1 (`SERGET.COM`), and executes live DOS commands.
- **`ci.yml`** — Main caller workflow triggered on `push` (`main`/`master`), `pull_request`, and `workflow_dispatch` with manual toggles for `msdos_qemu_testing`, `msdos_versions`, `fuzz_cases`, `run_docker`, and `run_coverage`.

## Local CI Replication

```sh
# List all local CI stages
./ci/replicate_ci.sh --list

# Run full local CI (excluding opt-in QEMU MS-DOS)
./ci/replicate_ci.sh

# Run fast local CI without Docker
./ci/replicate_ci.sh --no-docker

# Run a single stage
./ci/replicate_ci.sh --only lint
./ci/replicate_ci.sh --only emu-fuzz --fuzz-cases 1000
./ci/replicate_ci.sh --only docker-alpine

# Run real headless QEMU MS-DOS (1.25, 2.00, 4.00) & FreeDOS 1.3 tests locally
BENTO_DIR=../bento sh ./ci/run_dos_qemu_matrix.sh --os all --toolchain nasm
```

On native Windows:

```cmd
ci\replicate_ci.cmd
```
