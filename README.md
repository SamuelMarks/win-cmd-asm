win-cmd-asm
===========

[![License](https://img.shields.io/badge/license-Apache--2.0%20OR%20MIT%20OR%20CC0-blue.svg)](https://opensource.org/licenses/Apache-2.0)
[![CI](https://github.com/SamuelMarks/win-cmd-asm/actions/workflows/ci.yml/badge.svg)](https://github.com/SamuelMarks/win-cmd-asm/actions/workflows/ci.yml)
![doc coverage](https://img.shields.io/badge/doc%20coverage-100.00%25-brightgreen)
![test coverage](https://img.shields.io/badge/test%20coverage-85.00%25-green)

**100% CRT-free, canonical multi-ISA, multi-assembler, multi-OS assembly implementation of Windows Command Prompt (`CMD.EXE`), MS-DOS `COMMAND.COM`, a universal `.BAT` / `.CMD` / `.COM` / DOS `MZ` `.EXE` execution engine (including an embedded `vm8086` virtual 8086 CPU on 32/64-bit x86 and ARM), and the 19-binary companion command-line utility suite — backed by a strict zero-`unsafe` Rust lowering, static-analysis, 5-ISA emulation, differential fuzzing, and reproducible media-mastering workspace.**

> **Detailed Documentation**
> - **[ARCHITECTURE.md](ARCHITECTURE.md)** — Deep technical specification of all 5 CPU ISAs, canonical assembly lowering, CFG/ABI static verification, per-OS HALs, 5-phase batch/AST engine, embedded `vm8086`, 19 utilities, and 5-ISA emulators.
> - **[USAGE.md](USAGE.md)** — Complete build, self-hosting, startup-switch, batch-language, 47 built-in commands, 19 companion utilities, `vm8086`, Rust CLI tools, QEMU media booting, and CI replication manual.
> - **[ci/README.md](ci/README.md)** — Multi-OS, Docker, and real Bento/QEMU MS-DOS (`1.25`, `2.00`, `4.00`, `FreeDOS 1.3`) CI guide.
> - **[tests/README.md](tests/README.md)** — Cross-ISA `.BAT` / `.CMD` smoke, utility, and self-hosting regression scripts.

---

## Table of Contents

1. [Overview & Design Pillars](#overview--design-pillars)
2. [Multi-OS Compatibility Matrix](#multi-os-compatibility-matrix)
3. [Multi-ISA & Multi-Assembler Matrix](#multi-isa--multi-assembler-matrix)
4. [Built Artifacts, Cross-Assembler Equivalence & Footprint Budgets](#built-artifacts-cross-assembler-equivalence--footprint-budgets)
5. [Shell Capabilities, 47 Built-Ins & 19 Companion Utilities](#shell-capabilities-47-built-ins--19-companion-utilities)
6. [Rust Verification, Emulation, Fuzzing & Packaging Workspace](#rust-verification-emulation-fuzzing--packaging-workspace)
7. [Quick Build, Verification & Media Mastering](#quick-build-verification--media-mastering)
8. [Repository Layout](#repository-layout)
9. [License](#license)

---

## Overview & Design Pillars

`win-cmd-asm` implements compatible command processors, batch script engines, and standalone system utilities spanning **44+ years of PC and Windows operating systems** (`1982` MS-DOS `1.25` through `Windows 11` `x64` and `ARM64`) from a single canonical assembly source tree (`asm/`):

- **Zero C Runtime (`msvcrt` / `ucrt` / `libc`)**: Every binary enters directly at its raw OS loader entrypoint (`ORG 0x0100`, `mz_entry`, `w16_entry` / `w16_entry_286`, or `mainCRTStartup`) and talks exclusively to MS-DOS interrupts (`INT 20h`–`2Fh`, `INT 67h`), Win16 `KERNEL`/`USER`/`GDI`/`WNet`/`TOOLHELP` ordinals, or Windows `KERNEL32.DLL` / `COREDLL.DLL` (plus dynamic `ADVAPI32.DLL`, `MPR.DLL`, and `SHELL32.DLL` resolution when available).
- **Single Canonical Source, Six Assembler Backends**: Canonical `.asm` and `.inc` modules (`239` verified canonical modules + `26` native ABI/header/import shims under `asm/tc/`) lower losslessly via `tools/asm-conv` (and on 16-bit DOS hosts via `ASMCONV.COM`) into native **NASM**, **FASM** (including `fasm-pe32` direct PE32 and `fasm-coff` MS64 COFF), **JWasm**, **FASMARM**, **LLVM `llvm-mc`**, and **Microsoft `armasm` / `armasm64`**.
- **Embedded Multi-ISA `vm8086` 16-Bit Executor**: `WINCMD32.EXE`, `WINCMD64.EXE`, `WINCMD_ARMV4.EXE`, `WINCMD_ARM32.EXE`, and `WINCMD_ARM64.EXE` embed a pure-assembly `1 MiB + 64 KiB` virtual 8086 CPU + DOS `INT 21h`/`10h`/`16h`/`67h` syscall bridge, executing real 16-bit MS-DOS `.COM` and relocatable `MZ` `.EXE` programs in-process on 64-bit x64 and ARM Windows where NTVDM does not exist.
- **95 Companion Utility Binaries (`19` Tools × `5` ISA Families)**: Standalone CRT-free implementations of `ATTRIB`, `CHOICE`, `COMP`, `FC`, `FIND`, `FINDSTR` (Thompson NFA regex), `HOSTNAME`, `MEM`, `MORE`, `REG` (live `ADVAPI32` Registry), `REPLACE`, `ROBOCOPY` (`CreateThread` `/MT[:n]` worker pool), `SORT` (`O(N log N)` merge sort), `TIMEOUT`, `TREE`, `TRUENAME`, `WHERE`, `WHOAMI` (Token/SID), and `XCOPY`.
- **100% Enforced Assembly & Rust Documentation + Static Gates**: Every assembly file, `@proc`, `@struc`, and `@const` carries verified structured `@tag` contracts enforced by `asm-lint` alongside CFG stack-balance, `@clobbers`/ABI preservation, ISA opcode purity, cross-assembler binary equivalence, and byte-budget gates. All six Rust crates compile under `#![deny(missing_docs, unsafe_code, clippy::all, clippy::pedantic, clippy::nursery, clippy::unwrap_used, clippy::expect_used, clippy::panic)]` using a single `derive_more`-backed `ToolError` enum.

---

## Multi-OS Compatibility Matrix

| Era / OS Family | Target Releases | Primary Shell Binaries | Key Architectural Highlights |
| :--- | :--- | :--- | :--- |
| **MS-DOS 1.x** | MS-DOS `1.25` (1982) | `WINCMD1.COM`, `WINCMD.COM`, `WINCMDMZ.EXE` | `AL=0` `AH=30h` detection, FCB `wRecSize=1` byte-exact I/O, resident child `.COM` loader (`ldr_125`), `INT 21h` console redirection (`red_125`), `INT 25h` FAT12 free space. |
| **MS-DOS 2.x–6.x & FreeDOS** | MS-DOS `2.00`, `4.00`, `5.0–6.22`, FreeDOS `1.3+` | `WINCMD.COM`, `WINCMDMZ.EXE`, `WINCMD.MSG` + `19` `.COM` utilities | 3-part `RESIDENT` (`<= 2,816 B`) + `TRANSIENT` (`CRC-16` reload) + `INIT` split, `SS:SP`-guarded `AH=4Bh` `EXEC`, `FILES=8` zero-open-handle batching, `AX=6C00h`, `/MSG` extended errors, `EMS 4.0` swap, DBCS lead-byte awareness. |
| **Windows 9x DOS** | Windows `95`, `98`, `Me` (`DOS 7.0/7.1/8.0`) | `WINCMD.COM` / `WINCMD32.EXE` | `INT 21h AX=71xxh` Long Filenames (`LFN`), `AX=7303h` `FAT32` multi-GiB volume free space, `START` VMM/Shell integration. |
| **16-Bit Windows (`Win16`)** | Windows `1.01–1.04`, `2.03`, `2.1x`, `3.0`, `3.1/3.11`, WfW `3.11`, Chinese `3.2` | `WINCMD16.EXE`, `WINCMD286.EXE`, `WINCMD.PIF` | Multi-segment `NE` (`_TEXT`, `DGROUP`, discardable `_TRN286`), GDI scrollable terminal, selector-clean Protected Mode, `TOOLHELP` memory reporting, WfW 3.11 UNC `PUSHD`/`POPD`, Win 3.2 `GB2312`/`CP936` DBCS & IME, `545 B` `WINOLDAP` `.PIF`. |
| **Windows NT & 9x (`IA-32`)** | Windows `NT 3.1–4.0`, `2000`, `XP–10` (x86 / WOW64 on `11`), `95/98/Me` | `WINCMD32.EXE` + `19` `PE32` utilities | `PE32` (`Subsystem 3.10`), `/SAFESEH`, `DYNAMIC_BASE`, `NX_COMPAT`, dual Unicode `*W` (NT) / ANSI `*A` (Win9x) HAL vtables, progressive `GetProcAddress`, concurrent `CreatePipe`, embedded `vm8086`. |
| **Windows x64 (`AMD64`)** | Windows `XP Pro x64` / `Server 2003 SP1` (`5.2`) through `Windows 11` | `WINCMD64.EXE` + `19` `PE32+` utilities | `PE32+` (`Subsystem 5.02`), `.pdata`/`.xdata` SEH unwinds, `HIGH_ENTROPY_VA`, `GUARD_CF` (`CFG`), Intel `CET` shadow-stack compat, extended-length `32K` long paths, ConPTY/VT, embedded `x64` `vm8086`. |
| **Windows CE (`ARMv4`)** | Windows CE `3.0–6.0`, Pocket PC, Embedded Compact `7/2013` | `WINCMD_ARMV4.EXE` | Strict 32-bit `ARMv4` `PE32` (`0x01C0`, `Subsystem 9`), `COREDLL.DLL` Unicode imports, rootless `\Storage Card` paths, byte-safe unaligned helpers, software division, embedded `ARMv4` `vm8086`. |
| **Windows RT / NT (`ARMv7`)** | Windows RT `8.0/8.1`, Windows 10 IoT / Mobile `ARM32` | `WINCMD_ARM32.EXE` + `19` `ARMNT` utilities | `ARMv7-A` Thumb-2 `UAL` `PE32` (`0x01C4` `ARMNT`, `Subsystem 6.02`), strict `IT` (`<= 4`), `MRC p15` `TEB`, `.pdata` unwinds, `GUARD_CF`, embedded `ARMv7` `vm8086`. |
| **Windows on ARM (`ARM64`)** | Windows `10` (`1709+`) & `Windows 11` on `ARM64` | `WINCMD_ARM64.EXE` + `19` `ARM64` utilities | `AArch64` `PE32+` (`0xAA64`, `Subsystem 10.0`), strict `X18` (`TEB`) preservation, `16`-byte hardware `SP` alignment + `__chkstk`, `HINT`-encoded `PACIASP`/`AUTIASP` + `BTI c`, `GUARD_CF`, embedded `ARM64` `vm8086`. |

---

## Multi-ISA & Multi-Assembler Matrix

| Target Directory & Binaries | ISA Baseline | NASM | FASM | JWasm | FASMARM | LLVM `llvm-mc` + `lld-link` | `armasm` / `armasm64` |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| `asm/dos16/` (`WINCMD.COM`, `WINCMD1.COM`, `WINCMDMZ.EXE`, `ASMCONV.COM`, `WINCMD.MSG`, `19` `*.COM`) | `8086` Real Mode | Yes (`-f bin` / `-f obj`) | Yes (direct `COM` / `MZ`) | Yes (`-bin` / `-mz`) | — | — | — |
| `asm/win16/` (`WINCMD16.EXE`, `WINCMD286.EXE`, `WINCMD.PIF`) | `8086` & `80286` Protected Mode | Yes (direct `NE` / `BIN`) | Yes (direct `NE` / `BIN`) | Yes (`-bin` `NE` / `BIN`) | — | — | — |
| `asm/x86_32/` (`WINCMD32.EXE` + `19` `*.EXE`) | `i386` IA-32 | Yes (`win32` COFF + `lld-link`) | Yes (`COFF` + `lld-link` **&** direct `fasm-pe32`) | Yes (`-coff` + `lld-link`) | — | — | — |
| `asm/x86_64/` (`WINCMD64.EXE` + `19` `*.EXE`) | `x86-64` AMD64 | Yes (`win64` COFF + `lld-link`) | Yes (direct `PE64` **&** `fasm-coff` + `lld-link`) | Yes (`-win64` `PROC FRAME` + `lld-link`) | — | — | — |
| `asm/arm32/` (`WINCMD_ARMV4.EXE`, `WINCMD_ARM32.EXE` + `19` `*.EXE`) | `ARMv4` (`A32`) & `ARMv7-A` (Thumb-2) | — | — | — | Yes (direct `PE32`) | Yes (`armv4t` obj & `thumbv7a-pc-windows-msvc` `PE32`) | Lowered (`dist/src/armasm/`) |
| `asm/arm64/` (`WINCMD_ARM64.EXE` + `19` `*.EXE`) | `ARMv8.0-A` `AArch64` | — | — | — | Yes (direct `PE32+`) | Yes (`aarch64-pc-windows-msvc` `PE32+`) | Lowered (`dist/src/armasm/`) |

---

## Built Artifacts, Cross-Assembler Equivalence & Footprint Budgets

Every binary is verified in `make verify` by `asm-lint` for ISA opcode purity, cross-assembler byte/section equivalence, and hard RAM/disk footprint budgets:

| Artifact(s) | Binary Format | Measured Size(s) | Hard Budget | Enforced Verification Gates |
| :--- | :--- | :--- | :--- | :--- |
| `WINCMD.COM` | 3-part 8086 `.COM` | **35,632 B** (NASM / FASM / JWasm) | `<= 49,152 B` (48 KiB disk; `<= 2,816 B` low RAM) | Exact 3-way byte identity (`NASM == FASM == JWasm`), 8086 purity, 4-era `emu-cov` + `diff-fuzz` |
| `WINCMD1.COM` | 3-part DOS 1.25 `.COM` | **1,088 B** (NASM / FASM / JWasm) | `<= 20,480 B` (20 KiB) | Exact 3-way byte identity, 8086 purity, DOS `1.25` FCB `emu-cov` |
| `WINCMDMZ.EXE` | Relocatable 16-bit `MZ` | **176 B** (FASM / JWasm), **560 B** (NASM OMF) | `<= 4,096 B` (4 KiB) | Normalized `MZ` code-payload equivalence, 8086 purity |
| `ASMCONV.COM` | 8086 `.COM` lowerer | **1,375 B** (NASM / FASM / JWasm) | `<= 4,096 B` (4 KiB) | Exact 3-way byte identity, 8086 purity, DOS self-hosting test |
| `WINCMD.MSG` | DOS 4.00+ `/MSG` table | **169 B** (NASM / FASM / JWasm) | `<= 4,096 B` (4 KiB) | Exact 3-way byte identity |
| `19` × `dos16/*.COM` | Standalone 8086 `.COM` | **1,197 B – 2,044 B** each (**28,928 B** total) | `<= 12,288 B` each; `<= 77,824 B` total | Exact 3-way byte identity, 8086 purity, `WINCMD.COM` + `19` utilities = **64,560 B** (`<= 160 KiB` floppy!) |
| `WINCMD.PIF` | Win16 `WINOLDAP` `.PIF` | **545 B** (NASM / FASM / JWasm) | `<= 545 B` (`369 B` std + 286 + 386) | Exact 3-way byte identity |
| `WINCMD16.EXE` | 8086 Win16 `NE` | **11,264 B** (NASM / FASM / JWasm) | `<= 65,536 B` (64 KiB) | Exact 3-way byte identity, 8086 + selector purity, `Win16State` `emu-cov` |
| `WINCMD286.EXE` | 80286 PM Win16 `NE` | **11,776 B** (NASM / FASM / JWasm) | `<= 65,536 B` (64 KiB) | Exact 3-way byte identity, 286 + Protected-Mode selector purity |
| `WINCMD32.EXE` | IA-32 `PE32` (`Subsys 3.10`) | **56,832 B** (NASM / FASM-PE32 / JWasm), **57,344 B** (FASM) | `<= 131,072 B` (128 KiB) | 4-way PE `.text` equivalence, `386` purity, `X86PeVm` + `diff-fuzz` |
| `19` × `x86_32/*.EXE` | IA-32 `PE32` utilities | **5,632 B – 10,752 B** each | `<= 32,768 B` (32 KiB) each | 3-way PE `.text` equivalence, `386` purity |
| `WINCMD64.EXE` | x86-64 `PE32+` (`Subsys 5.02`) | **69,632 B** (NASM / FASM-COFF / JWasm), **69,120 B** (FASM) | `<= 131,072 B` (128 KiB) | 4-way PE `.text` equivalence, `x64` + `.pdata` SEH purity, `X86PeVm` + `diff-fuzz` |
| `19` × `x86_64/*.EXE` | x86-64 `PE32+` utilities | **6,656 B – 13,312 B** each | `<= 32,768 B` (32 KiB) each | 3-way PE `.text` equivalence, `x64` purity |
| `WINCMD_ARMV4.EXE` | ARMv4 WinCE `PE32` (`0x01C0`) | **65,024 B** (FASMARM) | `<= 131,072 B` (128 KiB) | Cross-assembler `.text`/`.obj` check, strict `ARMv4` purity (no `BLX`/`MOVW`/`UDIV`), `Arm32Vm` |
| `WINCMD_ARM32.EXE` | ARMv7 Thumb-2 `PE32` (`0x01C4`) | **50,688 B** (FASMARM), **52,224 B** (LLVM) | `<= 131,072 B` (128 KiB) | Cross-assembler equivalence, Thumb-2 `IT` purity, `Arm32Vm` + `diff-fuzz` |
| `19` × `arm32/*.EXE` | ARMv7 Thumb-2 utilities | **8,192 B – 12,288 B** each | `<= 32,768 B` (32 KiB) each | Cross-assembler equivalence, `ARMv7` purity |
| `WINCMD_ARM64.EXE` | AArch64 `PE32+` (`0xAA64`) | **69,120 B** (FASMARM / LLVM) | `<= 131,072 B` (128 KiB) | Cross-assembler equivalence, `X18`-untouched + `16 B` `SP` + `PAC`/`BTI` purity, `Arm64Vm` + `diff-fuzz` |
| `19` × `arm64/*.EXE` | AArch64 `PE32+` utilities | **9,728 B – 14,336 B** each | `<= 32,768 B` (32 KiB) each | Cross-assembler equivalence, `X18`-untouched `ARM64` purity |

---

## Shell Capabilities, 47 Built-Ins & 19 Companion Utilities

### Core Language & Runtime Engine
- **Stateless Byte-Offset Batch Engine**: Holds **zero open file handles** while executing commands (`open -> seek -> read line -> close`), guaranteeing compatibility with MS-DOS 2.00 `FILES=8` limits and self-modifying `.BAT`/`.CMD` scripts.
- **5-Phase Expansion & Compound AST**: Supports `%0..%9`, `%*`, full `%~dpnxfsatz$PATH:I` parameter/`FOR` modifiers, parse-time `%VAR%` and delayed `!VAR!` expansion (`/V:ON`), `:old=new` substitution, `:~start,len` slicing, dynamic variables (`%CD%`, `%DATE%`, `%TIME%`, `%RANDOM%`, `%ERRORLEVEL%`, `%CMDEXTVERSION%`, `%CMDCMDLINE%`), compound `( ... )` blocks, `&`, `&&`, `||`, concurrent `CreatePipe` / spooled `|` pipelines, and `FD 0..9` redirection (`<`, `>`, `>>`, `2>&1`).
- **11-Tier Signed 32-Bit `SET /A` Pratt Evaluator**: Full C-style precedence (`()`, `! ~ - +`, `* / %`, `+ -`, `<< >>`, `&`, `^`, `|`, `= *= /= %= += -= &= ^= |= <<= >>=`, `,`) with `INT 00h`-safe `0x80000000 / -1` overflow guards.
- **Structured Control Flow**: `IF` (`NOT`, `/I`, `ERRORLEVEL`, `EXIST`, `DEFINED`, `CMDEXTVERSION`, `==`, `EQU`/`NEQ`/`LSS`/`LEQ`/`GTR`/`GEQ`, `ELSE`), `FOR` (plain, `/D`, `/R`, `/L`, `/F` with `eol=`, `skip=`, `delims=`, `tokens=`, `usebackq`), `CALL :label` subroutines, and 32-level `SETLOCAL` / `ENDLOCAL` environment & CWD scoping.

### All 47 Internal Built-In Commands (`asm/inc/cmd_ids.inc`)
`ASSOC`, `BREAK`, `CALL`, `CD` (`CHDIR`), `CHCP`, `CLS`, `CMD` (`COMMAND`), `COLOR`, `COPY`, `CTTY`, `DATE`, `DEL` (`ERASE`), `DIR`, `DOSKEY`, `DPATH`, `ECHO`, `ENDLOCAL`, `EXIT`, `FOR`, `FTYPE`, `GOTO`, `IF`, `KEYS`, `LH` (`LOADHIGH`), `MD` (`MKDIR`), `MKLINK` (`/D`, `/H`, `/J`), `MODE`, `MOVE`, `PATH`, `PAUSE`, `POPD`, `PROMPT`, `PUSHD`, `RD` (`RMDIR` — reparse-safe `/S /Q`), `REM`, `REN` (`RENAME`), `SET` (`/A`, `/P`), `SETLOCAL`, `SHIFT`, `START`, `TIME`, `TITLE`, `TRUENAME`, `TYPE`, `VER`, `VERIFY`, `VOL`.

### All 19 Standalone Companion Utilities (`asm/*/util/`)
`ATTRIB`, `CHOICE`, `COMP`, `FC`, `FIND`, `FINDSTR`, `HOSTNAME`, `MEM`, `MORE`, `REG`, `REPLACE`, `ROBOCOPY`, `SORT`, `TIMEOUT`, `TREE`, `TRUENAME`, `WHERE`, `WHOAMI`, `XCOPY` — compiled natively as `.COM` (`dos16`), `PE32` (`x86_32`, `arm32`), and `PE32+` (`x86_64`, `arm64`).

See **[USAGE.md](USAGE.md)** for complete syntax, switches, and examples for every built-in and companion utility.

---

## Rust Verification, Emulation, Fuzzing & Packaging Workspace

The Cargo workspace (`Cargo.toml`, Rust Edition 2024) contains **six** zero-`unsafe` crates:

| Crate | Path | Purpose & Key CLI Usage |
| :--- | :--- | :--- |
| **`wincmd-diag`** | `tools/wincmd-diag/` | Unified `ErrCode` (`1..=511`, synchronized with `asm/inc/err_code.inc`), `CpuIsa`, `TargetAssembler`, `SourceSpan`, and the single workspace-wide `derive_more` `ToolError` enum. |
| **`asm-conv`** | `tools/asm-conv/` | Canonical assembly parser and lossless 6-assembler lowering engine (`nasm`, `fasm`, `fasm-pe32`, `fasm-coff`, `jwasm`, `fasmarm`, `llvm-mc`, `armasm`).<br>`cargo run --release -p asm-conv -- --in-dir asm --out-dir dist/src` |
| **`asm-lint`** | `tools/asm-lint/` | 100% `@tag` documentation gate, FAT 8.3 path gate, CFG stack-balance & `@clobbers`/ABI verifier, ISA/selector/`X18`/SEH purity disassembler, cross-assembler binary equivalence checker, and byte-budget gate.<br>`cargo run --release -p asm-lint -- --dir asm` |
| **`emu-cov`** | `tools/emu-cov/` | Deterministic 5-ISA user-mode & Real/Protected-Mode emulator (`DosVm` across DOS `1.25`/`2.00`/`4.00`/`7.10`, `Win16State` `NE`, `X86PeVm` `IA-32`/`x64` with `CreateThread`, `Arm32Vm` `ARMv4`/`ARMv7`, `Arm64Vm` `AArch64`) + `lcov.info` branch-edge reporter.<br>`cargo run --release -p emu-cov -- --com build/nasm/dos16/WINCMD.COM --era 4.00 --args "/C VER" --lcov lcov.info`<br>`cargo run --release -p emu-cov -- --pe build/nasm/x86_64/WINCMD64.EXE --args "/C VER"` |
| **`diff-fuzz`** | `tools/diff-fuzz/` | Deterministic `SplitMix64` batch/`.com` generator & differential fuzzer across NASM/FASM/JWasm DOS binaries, all 7 cross-ISA primary targets (`--cross-isa`), and live reference `cmd.exe` (`--ref-cmd`).<br>`cargo run --release -p diff-fuzz -- --cross-isa --seed 0x20261123 --cases 10000 --ref-cmd 25 --build-dir build` |
| **`media-pkg`** | `tools/media-pkg/` | Deterministic release media masterer producing `160K`/`360K`/`720K`/`1.44M` FAT12 `.IMG` floppies, universal ISO-9660 `.ISO`, per-ISA `.ZIP` archives, zero-Rust standalone source `.tar.gz` tarballs (`nasm`/`fasm`/`jwasm`), and `SHA256SUMS.txt`.<br>`cargo run --release -p media-pkg -- --root . --build-dir build --dist-src dist/src --out-dir dist/release` |

---

## Quick Build, Verification & Media Mastering

```bash
# Full pipeline: lint -> lower -> multi-assembler 5-ISA build -> verify -> Rust/emulator tests
make all

# Individual stages (filterable with TOOLCHAIN=nasm|fasm|jwasm|fasmarm|llvm and TARGET=dos16|win16|x86_32|x86_64|arm32|arm64)
make lint          # Run asm-lint across all 239 canonical assembly modules
make lower         # Lower asm/ into dist/src/{nasm,fasm,fasm-pe32,fasm-coff,jwasm,fasmarm,llvm-mc,armasm}
make build         # Assemble & link all shell and 19-utility binaries into build/
make verify        # Enforce cross-assembler equivalence, ISA purity, and footprint budgets
make test          # Run Rust unit/integration suites + 5-ISA emu-cov smoke runs
make fuzz          # Run 2,500 DOS cross-assembler + 10,000 7-ISA + 25 live cmd.exe differential fuzz cases
make package       # Master 13 reproducible .img, .iso, .zip, and zero-Rust .tar.gz release artifacts
make coverage      # Report Rust workspace coverage via cargo-llvm-cov
make hooks         # Install strict POSIX/Windows pre-commit quality & EOL hooks
```

On native Windows (`NT 3.1` through `Windows 11`) or native MS-DOS (`2.00`+):

```bat
make.bat all
```

See **[USAGE.md](USAGE.md)** for toolchain bootstrap options, cross-compilation setups, QEMU DOS matrix execution (`make ci-dos-qemu`), and standalone zero-Rust builds.

---

## Repository Layout

```text
win-cmd-asm/
├── asm/
│   ├── inc/                  # Shared canonical constants & STRUCs (err_code, ast_kind, cmd_ids, dos/ne/pe)
│   ├── tc/                   # Assembler ABI & header shims (nasm/, fasm/, jwasm/, arm_ms/ + .def imports)
│   ├── dos16/                # 8086 3-part WINCMD.COM, WINCMD1.COM, WINCMDMZ.EXE, WINCMD.MSG, ASMCONV.COM + 19 *.COM
│   ├── win16/                # 8086/286 Win16 NE WINCMD16.EXE, WINCMD286.EXE & 545-byte WINCMD.PIF
│   ├── x86_32/               # IA-32 PE32 WINCMD32.EXE (NT 3.1..11 & Win9x), vm8086 + 19 *.EXE utilities
│   ├── x86_64/               # x86-64 PE32+ WINCMD64.EXE (XP x64..Win11, SEH/CFG/CET), vm8086 + 19 *.EXE utilities
│   ├── arm32/                # ARMv4 WinCE (v4/) & ARMv7-A Thumb-2 ARMNT (v7/) shells, vm8086 + 19 *.EXE utilities
│   └── arm64/                # AArch64 PE32+ WINCMD_ARM64.EXE (X18-safe, PAC/BTI, CFG), vm8086 + 19 *.EXE utilities
├── tools/                    # Zero-unsafe Rust 2024 verification, emulation, fuzzing & packaging workspace
│   ├── wincmd-diag/          # Unified ErrCode (1..=511), CpuIsa, TargetAssembler, SourceSpan, ToolError
│   ├── asm-conv/             # Canonical assembly parser & 6-assembler lowering engine
│   ├── asm-lint/             # 100% @tag doc, FAT 8.3, CFG stack/clobber/ABI, purity, equivalence & budget gates
│   ├── emu-cov/              # Deterministic 5-ISA DOS/Win16/PE32/PE32+ emulators & LCOV branch-edge reporter
│   ├── diff-fuzz/            # SplitMix64 workload generator, 7-ISA & live cmd.exe differential fuzzer
│   └── media-pkg/            # Deterministic FAT12 .IMG, ISO-9660 .ISO, .ZIP & zero-Rust .tar.gz masterer
├── tests/                    # Portable FAT-8.3 .BAT/.CMD smoke, utility & Windows self-hosting scripts
├── ci/                       # Cross-OS local CI replicators, Dockerfiles & Bento/QEMU MS-DOS boot matrix
├── scripts/                  # POSIX build-all.sh and multi-assembler bootstrap-toolchains.sh
├── .githooks/                # POSIX (pre-commit) and Windows (pre-commit.cmd) 6-stage quality & EOL hooks
├── ARCHITECTURE.md           # Deep multi-ISA systems, lowering, static analysis, HAL & runtime architecture
├── USAGE.md                  # Comprehensive build, CLI switch, batch language, built-in & utility manual
├── Makefile                  # GNU Make orchestrator (macOS / Linux / MSYS2)
├── make.bat                  # Universal Windows NT / MS-DOS build entrypoint
├── BUILD.CMD / BUILD.BAT     # Native Windows NT and strict-8.3 MS-DOS self-hosting build scripts
└── Cargo.toml                # Workspace manifest and strict clippy/lint configuration
```

---

---

## 📜 License

Licensed under any of:

- Apache License, Version 2.0 ([LICENSE-APACHE](LICENSE-APACHE) or
  <https://apache.org/licenses/LICENSE-2.0>)
- MIT license ([LICENSE-MIT](LICENSE-MIT) or <https://opensource.org/licenses/MIT>)
- CC0 1.0 Universal ([LICENSE-CC0](LICENSE-CC0) or
  <https://creativecommons.org/publicdomain/zero/1.0/>)

at your option.
