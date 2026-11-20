# win-cmd-asm

![doc coverage](https://img.shields.io/badge/doc%20coverage-100.00%25-brightgreen)
![test coverage](https://img.shields.io/badge/test%20coverage-88.95%25-green)

**CRT-free, canonical multi-ISA, multi-assembler, multi-OS assembly implementation of Windows Command Prompt (`CMD.EXE`), MS-DOS `COMMAND.COM`, and `.BAT` / `.CMD` / `.COM` / `.EXE` execution engines — backed by a strict zero-`unsafe` Rust lowering, static-analysis, 8086/286 emulation, and differential fuzzing suite.**

---

## Table of Contents

1. [Overview & Design Pillars](#overview--design-pillars)
2. [Multi-OS Compatibility Matrix](#multi-os-compatibility-matrix)
3. [Multi-ISA Architecture Matrix](#multi-isa-architecture-matrix)
4. [Multi-Assembler & Self-Hosting Lowering Pipeline](#multi-assembler--self-hosting-lowering-pipeline)
5. [Built Artifacts, Cross-Assembler Equivalence & Footprint Budgets](#built-artifacts-cross-assembler-equivalence--footprint-budgets)
6. [Deep Dive: DOS16 3-Part Architecture (`WINCMD.COM`)](#deep-dive-dos16-3-part-architecture-wincmdcom)
   - [Resident / Transient / Init Memory Model](#resident--transient--init-memory-model)
   - [Era-Dispatching HAL & Cold-Boot Detection](#era-dispatching-hal--cold-boot-detection)
   - [4-Phase Batch Engine, Arena AST Parser & Interactive Frontend](#4-phase-batch-engine-arena-ast-parser--interactive-frontend)
   - [DOS16 Startup Switches & Dialect Selection](#dos16-startup-switches--dialect-selection)
   - [Internal Built-In Commands](#internal-built-in-commands)
7. [Win16, Win32, Win64, ARM32 & ARM64 Targets](#win16-win32-win64-arm32--arm64-targets)
8. [Canonical Assembly Dialect & 100% `@tag` Documentation](#canonical-assembly-dialect--100-tag-documentation)
9. [Rust Verification, Emulation & Fuzzing Workspace](#rust-verification-emulation--fuzzing-workspace)
10. [Building, Testing & Verification](#building-testing--verification)
    - [POSIX (macOS / Linux / MSYS2)](#posix-macos--linux--msys2)
    - [Windows NT (3.1 .. 11)](#windows-nt-31--11)
    - [Self-Hosting Native MS-DOS (2.00 / 4.00 / 7.x)](#self-hosting-native-ms-dos-200--400--7x)
    - [Pre-Commit Quality Gate](#pre-commit-quality-gate)
11. [Continuous Integration (`c-ci`-Inspired Pipeline)](#continuous-integration-c-ci-inspired-pipeline)
12. [Repository Layout](#repository-layout)
13. [License](#license)

---

## Overview & Design Pillars

`win-cmd-asm` implements the DOS/Windows command shell family from **MS-DOS 1.25 (1982)** through **Windows 11 (x64 & ARM64)** in pure, CRT-free assembly without relying on C runtime libraries:

- **Single Canonical Source -> 6 Assembler Backends:** Every assembly module under `asm/` is authored once in a portable canonical syntax (`@cpu`, `@format`, `@section`, `@proc`, `@proc_frame`, `@struc`, `@const`, `@data`, `@res`) and lowered deterministically to **NASM**, **FASM**, **JWasm**, **FASMARM**, **LLVM-MC**, and **ARMASM** — plus a self-hosted **8086 MS-DOS binary (`ASMCONV.COM`, 1,375 bytes)** capable of lowering canonical `.asm` sources natively on vintage DOS hardware.
- **Cross-Assembler Equivalence Verification:** Artifacts built across independent assemblers (e.g., NASM vs. FASM vs. JWasm) are machine-verified for byte-for-byte / instruction-stream equivalence via `asm-lint --equiv` and behavioral equivalence via deterministic differential fuzzing (`diff-fuzz`).
- **Strict ISA & ABI Purity:** Static CFG stack-balance checks, `@clobbers`/ABI contract verification, hardware opcode purity verification (`8086`, `286`, `386`, `x64`, `ARMv4`, `ARMv7-A`, `ARM64`), Win16 selector safety, and PE/NE/COM header verification are enforced on every build.
- **100% Documentation & Strict Rust Standards:** Every assembly file, constant, structure, and procedure requires structured `;; @tag` documentation (`asm-lint`). The Rust verification workspace enforces Rust Edition 2024, `#![deny(missing_docs)]`, `#![deny(unsafe_code)]`, `clippy::{all, pedantic, nursery}`, zero `unwrap`/`expect`/`panic`, and a single `derive_more`-backed `ToolError` enum aligned with canonical `ERR_*` codes (`1..=511`).

---

## Multi-OS Compatibility Matrix

| OS Family | Supported Versions | Binary Artifact(s) | Executable Format & Subsystem | Key Era Capabilities |
| :--- | :--- | :--- | :--- | :--- |
| **MS-DOS 1.x** | MS-DOS 1.25 | `WINCMD.COM` | 3-part flat `.COM` (`ORG 0x0100`) | Pure 8086, FCB/XFCB byte-granular I/O (`INT 21h` `0Fh`–`28h`), `INT 25h` FAT12 free space, manual PSP + `.COM` / relocated MZ `.EXE` loader, resident `INT 21h` FCB character-I/O redirector (`<`, `>`, `>>`, `\|`) |
| **MS-DOS 2.x–3.x** | MS-DOS 2.00+ | `WINCMD.COM` | 3-part flat `.COM` (`ORG 0x0100`) | ASCIIZ handle I/O (`3Ch`–`46h`), hierarchical directories, stacked DTA search, stateless `FILES=8` batch runner (0 open `.BAT` handles during command execution), `SS:SP`-safe `INT 21h AX=4B00h` `EXEC` trampoline |
| **MS-DOS 4.x–6.x** | MS-DOS 4.00+ | `WINCMD.COM`, `WINCMD.MSG` | 3-part `.COM` + external `/MSG` table (`WMSG`) | >32 MiB FAT16 64-bit free-space calculation, `AX=6C00h` extended open, `AX=6900h` volume serial, `AH=5Ah`/`60h`/`59h`, NLS codepage (`AX=6601h`/`6602h`) & DBCS lead-byte bitmap (`AX=6507h`), `INT 24h` Fail, LIM EMS 4.0 / XMS transient reload |
| **MS-DOS 7.x–8.0 (Win9x DOS)** | Windows 95, 98, Me (DOS 7.0/7.10/8.0) | `WINCMD.COM` | 3-part flat `.COM` (`ORG 0x0100`) | Long File Names (`INT 21h AX=71xxh`), >2 GiB FAT32 free space (`AX=7303h`), WINOLDAP Enhanced (`INT 2Fh AX=1600h`) & Standard (`AX=4680h`) detection |
| **Windows 16-bit (Real / Standard / Enhanced)** | Windows 1.0, 2.0, 2.1x, 3.0, 3.1, 3.11, 3.2 | `WINCMD16.EXE`, `WINCMD286.EXE`, `WINCMD.PIF` | Win16 New Executable (`NE`, `expver 1.01` 8086 & `expver 3.00` 80286) + 369-byte WINOLDAP `.PIF` | Real-mode 8086 universal NE (`WINCMD16.EXE`), Protected-Mode 80286 NE (`WINCMD286.EXE`) with selector-safety linting, classic 369-byte `WINCMD.PIF` |
| **Windows 9x & NT 32-bit** | Windows 95, 98, Me; NT 3.1, 3.5, 3.51, 4.0, 2000, XP, Vista, 7, 8, 8.1, 10, 11 | `WINCMD32.EXE` | CRT-free `PE32` (`i386`, `Subsystem 3.10`, `CONSOLE`) | Direct `KERNEL32.DLL` imports (`GetStdHandle`, `WriteFile`, `GetCommandLineA`, `GetVersion`, `ExitProcess`), pure i386 baseline without `CMOV`/`SSE` |
| **Windows 64-bit (x64)** | Windows XP Pro x64, Vista, 7, 8, 8.1, 10, 11 | `WINCMD64.EXE` | CRT-free `PE32+` (`AMD64`, `Subsystem 5.02`, `/HIGHENTROPYVA /DYNAMICBASE /NXCOMPAT`) | Microsoft x64 ABI with 32-byte shadow space, 16-byte `RSP` alignment, and `.pdata`/`.xdata` SEH unwind frames via `@proc_frame` |
| **Windows CE / Embedded** | Windows CE, Pocket PC, Embedded | `WINCMD_ARMV4.EXE` | CRT-free `PE32` (`Machine 0x01C0`, ARMv4) | Strict ARMv4 32-bit RISC (zero `CLZ`, zero `UDIV`, zero `BLX`), restoring 32-bit unsigned division, 5-bit shift masking, 8-byte `SP` alignment |
| **Windows RT & Win10 ARM32** | Windows RT 8.0, 8.1; Windows 10 ARM32 | `WINCMD_ARM32.EXE` | CRT-free `PE32` (`Machine 0x01C4 ARMNT`, `Subsystem 6.02`, Thumb-2 UAL) | `ARMv7-A` Thumb-2 UAL, `AAPCS-VFP`, trap-safe `SDIV` guarding `/0` and `INT32_MIN / -1`, `IT`-clean control flow |
| **Windows on ARM64** | Windows 10 ARM64, Windows 11 ARM64 | `WINCMD_ARM64.EXE` | CRT-free `PE32+` (`Machine 0xAA64`, `Subsystem 10.0`, `/HIGHENTROPYVA /DYNAMICBASE /NXCOMPAT`) | AArch64 ABI, 16-byte `SP` alignment, strict `X18` (Windows TEB register) preservation verified statically, trap-safe `SDIV` |

---

## Multi-ISA Architecture Matrix

| ISA Family | Canonical `@cpu` | Target Binaries | ABI & Purity Guarantees Enforced by `asm-lint` |
| :--- | :--- | :--- | :--- |
| **x86-16 (8086 / 8088)** | `8086` | `WINCMD.COM`, `WINCMD.MSG`, `ASMCONV.COM`, `WINCMD16.EXE`, `WINCMD.PIF` | Zero 186+ instructions (no `pusha`/`popa`, no `push imm`, no `shl reg, imm>1`, no `enter`/`leave`, no `imul reg, r/m, imm`); INT `00h`-free software 32/64-bit arithmetic |
| **x86-16 (80286 PM)** | `286` | `WINCMD286.EXE` | Protected-mode 80286 NE with Win16 segment/selector arithmetic safety checks (rejects real-mode segment math on selectors) |
| **IA-32 (x86)** | `386` | `WINCMD32.EXE` | Strict Intel 80386 baseline (NT 3.1 compatible), `stdcall` Win32 ABI, CRT-free PE32 purity check |
| **x86-64 (AMD64)** | `x64` | `WINCMD64.EXE` | Microsoft x64 calling convention (`RCX/RDX/R8/R9`, 32-byte shadow space, 16-byte stack alignment), `.pdata`/`.xdata` SEH frames (`@proc_frame`), RIP-relative addressing |
| **ARM32 (ARMv4 & ARMv7-A)** | `armv4`, `armv7` | `WINCMD_ARMV4.EXE`, `WINCMD_ARM32.EXE` | `armv4`: strict ARMv4 opcode set without `CLZ`/`UDIV`/`BLX` and explicit `and r1, r1, #31` shift masking; `armv7`: Thumb-2 UAL (`ARMNT`, `HWDiv`) with `/0` & `INT32_MIN/-1` guards |
| **ARM64 (AArch64)** | `arm64` | `WINCMD_ARM64.EXE` | Windows ARM64 ABI, 16-byte `SP` alignment, strict read-only preservation of platform register `X18` (TEB) |

---

## Multi-Assembler & Self-Hosting Lowering Pipeline

```text
                       Canonical Multi-ISA Sources (asm/**/*.asm, asm/**/*.inc)
                                                │
                       ┌────────────────────────┴────────────────────────┐
                       │                                                 │
             [Host: Rust `asm-conv`]                         [Self-Hosted DOS16: `ASMCONV.COM`]
         (parses AST + lowers all 6 backends)                   (/N | /F | /J, 1,375 bytes)
                       │                                                 │
    ┌──────────┬───────┴────┬──────────┬──────────┬──────────┐           └─► NASM / FASM / JWasm
    ▼          ▼            ▼          ▼          ▼          ▼               (native MS-DOS build)
  NASM       FASM         JWasm     FASMARM    LLVM-MC    ARMASM
 (x86 16/  (x86 16/     (x86 16/   (ARMv4/v7/  (ARMv4/v7/ (ARM32/64
  32/64)    32/64)       32/64)     ARM64 PE)   ARM64 COFF) MS shims)
    │          │            │          │          │
    └────┬─────┴──────┬─────┘          │       lld-link
         │         lld-link            │    (PE32 / PE32+)
         │      (COFF -> PE32/64)      │          │
         ▼            ▼                ▼          ▼
   Flat .COM /   WINCMD32.EXE     WINCMD_ARM*  WINCMD_ARM*
   .MSG / .PIF / WINCMD64.EXE       .EXE         .EXE
   NE .EXE
         │            │                │          │
         └────────────┴────────────────┴──────────┘
                                │
             [asm-lint: --equiv, --purity, --budget]
             [emu-cov: 8086/286 4-era execution + LCOV]
             [diff-fuzz: SplitMix64 cross-assembler differential test]
```

### Assembler Support Matrix

| Target | NASM | FASM | JWasm | FASMARM | LLVM-MC + `lld-link` | ARMASM (`asm/tc/arm_ms/`) |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **DOS16** (`WINCMD.COM`, `WINCMD.MSG`, `ASMCONV.COM`) | Yes (`-f bin`) | Yes | Yes (`-bin`) | — | — | — |
| **Win16** (`WINCMD16.EXE`, `WINCMD286.EXE`, `WINCMD.PIF`) | Yes (`-f bin`) | Yes | Yes (`-bin`) | — | — | — |
| **IA-32** (`WINCMD32.EXE`) | Yes (`-f win32` + `lld-link`) | Yes (COFF + `lld-link`) | Yes (`-coff` + `lld-link`) | — | — | — |
| **x86-64** (`WINCMD64.EXE`) | Yes (`-f win64` + `lld-link`) | Yes (direct `PE32+`) | Yes (`-win64` + `lld-link`) | — | — | — |
| **ARMv4** (`WINCMD_ARMV4.EXE`) | — | — | — | Yes (direct `PE32`) | Yes (`armv4t-none-eabi` obj) | Lowering + ABI shims |
| **ARMv7-A** (`WINCMD_ARM32.EXE`) | — | — | — | Yes (direct `PE32`) | Yes (`thumbv7a-pc-windows-msvc` + `lld-link`) | Lowering + ABI shims |
| **ARM64** (`WINCMD_ARM64.EXE`) | — | — | — | Yes (direct `PE32+`) | Yes (`aarch64-pc-windows-msvc` + `lld-link`) | Lowering + ABI shims |

Toolchain shims and import definitions live under `asm/tc/` (`nasm/`, `fasm/`, `jwasm/`, `arm_ms/` including `k32_x86.def`, `k32_base.def`, and `coredll.def`).

---

## Built Artifacts, Cross-Assembler Equivalence & Footprint Budgets

Every binary is verified by `asm-lint` for strict size budgets, ISA purity, and cross-assembler equivalence:

| Artifact | Format | Measured Size(s) | Footprint Budget | Verification Gates |
| :--- | :--- | :--- | :--- | :--- |
| `WINCMD.COM` | 3-part 8086 `.COM` | **14,384 B** (NASM / FASM / JWasm) | `<= 49,152 B` (48 KiB) | 3-way equivalence (`NASM == FASM == JWasm`), 8086 purity, 4-era `emu-cov` + `diff-fuzz` |
| `ASMCONV.COM` | 8086 `.COM` lowerer | **1,375 B** (NASM / FASM / JWasm) | `<= 4,096 B` (4 KiB) | 3-way equivalence (`NASM == FASM == JWasm`), 8086 purity |
| `WINCMD.MSG` | DOS 4.00+ `/MSG` table | **169 B** (NASM / FASM / JWasm) | `<= 4,096 B` (4 KiB) | 3-way equivalence (`NASM == FASM == JWasm`) |
| `WINCMD.PIF` | Win16 WINOLDAP `.PIF` | **369 B** (NASM / FASM / JWasm) | `== 369 B` (exact) | 3-way equivalence (`NASM == FASM == JWasm`) |
| `WINCMD16.EXE` | 8086 Win16 NE | **448 B** (NASM / FASM / JWasm) | `<= 65,536 B` (64 KiB) | 3-way equivalence (`NASM == FASM == JWasm`), 8086 purity |
| `WINCMD286.EXE` | 80286 PM Win16 NE | **448 B** (NASM / FASM / JWasm) | `<= 65,536 B` (64 KiB) | 3-way equivalence (`NASM == FASM == JWasm`), selector safety |
| `WINCMD32.EXE` | IA-32 PE32 (Subsys 3.10) | **2,048 B** (NASM/FASM), **2,560 B** (JWasm) | `<= 131,072 B` (128 KiB) | 3-way code-payload equivalence, i386 PE purity |
| `WINCMD64.EXE` | x86-64 PE32+ (Subsys 5.02) | **2,560 B** (NASM/JWasm), **3,584 B** (FASM) | `<= 131,072 B` (128 KiB) | 3-way code-payload equivalence, x64 PE purity |
| `WINCMD_ARMV4.EXE` | ARMv4 PE32 (`0x01C0`) | **1,536 B** (FASMARM) + LLVM COFF obj | `<= 131,072 B` (128 KiB) | ARMv4 PE purity |
| `WINCMD_ARM32.EXE` | ARMv7-A PE32 (`0x01C4`) | **1,536 B** (FASMARM), **2,048 B** (LLVM) | `<= 131,072 B` (128 KiB) | ARMv7 PE purity |
| `WINCMD_ARM64.EXE` | ARM64 PE32+ (`0xAA64`) | **1,536 B** (FASMARM), **2,048 B** (LLVM) | `<= 131,072 B` (128 KiB) | ARM64 PE purity (`FASMARM` & `LLVM`) |

---

## Deep Dive: DOS16 3-Part Architecture (`WINCMD.COM`)

### Resident / Transient / Init Memory Model

`WINCMD.COM` follows the authentic three-section MS-DOS `COMMAND.COM` memory architecture (`@format com3part`) so child programs receive maximum free conventional RAM:

```text
Low Conventional Memory                                    Top of Conventional Memory
┌───────────────────────────┬─────────────────────────────┬──────────────────────────┐
│ PSP + RESIDENT (0x0100)   │  Free Conventional RAM      │ TRANSIENT (wTrnSeg:0000) │
│ - RES_STATE & batch stack │  (available to child .COM / │ - 16 KiB reserve (0x0400 │
│ - INT 23h Ctrl+C ISR      │   .EXE after INIT is freed  │   paragraphs)            │
│ - INT 24h Critical Error  │   via INT 21h AH=4Ah)       │ - Code & rodata          │
│ - DOS 1.25 FCB redirector │                             │ - 0x2A00 Scratch vars    │
│ - DOS 1.25 .COM/MZ loader │                             │ - 0x2B00 History (128 B) │
│ - DOS 2.00+ EXEC tramp.   │                             │ - 0x2B80 Cmd buf (256 B) │
│ - 3-tier EMS/XMS/%COMSPEC%│                             │ - 0x2C80..0x38FE Arena   │
│   CRC-16 integrity reload │                             │   (0xA55A OOM canary)    │
│                           │                             │ - 0x3FFE Transient stack │
└───────────────────────────┴─────────────────────────────┴──────────────────────────┘
```

- **`RESIDENT` (`asm/dos16/res/`):** Remains permanently anchored at low memory. Installs `INT 23h` (`int_23.inc` async Ctrl+C/Ctrl+Break latch) and `INT 24h` (`int_24.inc` Abort/Retry/Ignore on DOS 1–2, Fail on DOS 4+), provides the DOS 1.25 manual PSP / `.COM` / relocated MZ `.EXE` loader (`ldr_125.inc`), DOS 1.25 `INT 21h` character-I/O FCB redirector (`red_125.inc`), DOS 2.00+ `SS:SP`-destruction-proof `AX=4B00h` `EXEC` trampoline (`exec_200.inc`), and the tableless CRC-16-CCITT (`crc16.inc`) integrity verifier that reloads `TRANSIENT` from **LIM EMS 4.0 -> XMS -> `%COMSPEC%`** (`ems_40.inc`) if a child program overwrites high conventional memory.
- **`TRANSIENT` (`wTrnSeg:0x0000`):** Relocated to the top of conventional RAM with an OOM-safe word-aligned bump arena (`arena.inc`, `0xA55A` canary), command history/recall buffer, parser, AST executor, and built-in commands.
- **`INIT` (`asm/dos16/init/det_dos.inc`):** Executes once at cold boot to detect the OS version, hardware capabilities, WINOLDAP, EMS/XMS, and startup switches, relocates `TRANSIENT`, computes its CRC-16 checksum, and is then discarded via MCB shrink (`INT 21h AH=4Ah`).

### Era-Dispatching HAL & Cold-Boot Detection

At startup, `det_dos.inc` probes the live DOS environment and dispatches file/volume/directory operations across four modular Hardware Abstraction Layers (`asm/dos16/hal/`):

- **`k_dos125.inc` (MS-DOS 1.25):** FCB/XFCB byte-granular open/read/write/create/delete/rename/search and direct `INT 25h` FAT12 sector reads for free-space reporting.
- **`k_dos200.inc` (MS-DOS 2.00+):** ASCIIZ handle I/O, directory creation/removal/traversal, stacked DTA wildcard searches, and stateless `FILES=8`-safe batch file line retrieval.
- **`k_dos400.inc` (MS-DOS 4.00+):** >32 MiB FAT16 64-bit free-space calculation, extended open (`AX=6C00h`), volume serial queries (`AX=6900h`), temp files (`AH=5Ah`), canonical paths (`AH=60h`), extended error retrieval (`AH=59h`), and NLS/DBCS codepage support (`cp_dbcs.inc`, `AX=6601h`/`6602h`/`6507h`).
- **`k_dos700.inc` (MS-DOS 7.0/7.10/8.0):** Windows 95/98/Me Long File Name (`LFN`) syscalls (`AX=71xxh`) and >2 GiB FAT32 free-space queries (`AX=7303h`).

### 4-Phase Batch Engine, Arena AST Parser & Interactive Frontend

- **Interactive Line Editor (`read/line_ed.inc`):** Dual-backend interactive console editor supporting both IBM PC BIOS (`INT 16h`/`INT 10h`) and pure DOS TTY (`ANSI`/`VT52`) terminals with template/recall history.
- **Stateless Batch Reader (`read/stream.inc`):** Byte-offset stateless reader supporting `UTF-8`, `UTF-16LE`, and `UTF-16BE` BOM detection, physical `0x1A` EOF handling, mixed `CR`/`LF`/`CRLF` line endings, and caret (`^`) line folding without holding `.BAT` file handles open across child commands.
- **4-Phase Expansion & Execution (`parse/` & `rt/`):**
  1. **Phase 1 (`phase1.inc`, `var_ops.inc`, `modifier.inc`):** Immediate `%0`–`%9`, `%*`, `%~[f|d|p|n|x|s|a|t|z|$VAR:]` parameter modifiers, dynamic variables, `%VAR%` expansion, substring (`%VAR:~s,l%`), and search/replace (`%VAR:a=b%`).
  2. **Phase 2a & 2b (`lexer.inc`, `prs_ast.inc`):** Context-sensitive tokenizer (quotes, `^` escapes, DBCS/UTF-8 lead bytes, `@`, `REM`, `ECHO<delim>`) feeding a recursive-descent Arena AST parser for parenthesized command blocks, `IF`/`ELSE`, `FOR`, pipelines (`|`), conditional chains (`&&`, `||`, `&`), and redirections (`<`, `>`, `>>`, FD `0..9`).
  3. **Phase 3 & 4 (`phase34.inc`):** `FOR` loop variable expansion (`%I` / `%~I`), delayed expansion (`!VAR!`) and `^` stripping, AST execution, pipes, `GOTO`, `CALL :label`, and `SHIFT`.
  4. **Arithmetic (`rt/arith16.inc`, `core/math32.inc`, `core/math64.inc`):** Precedence-climbing 32-bit signed `SET /A` evaluator (`+`, `-`, `*`, `/`, `%`, `<<`, `>>`, `&`, `^`, `|`, parentheses) backed by `INT 00h`-free 32-bit math and 64-bit comma-grouped volume size formatting.

### DOS16 Startup Switches & Dialect Selection

```text
WINCMD [/P] [/C cmd] [/K cmd] [/S] [/Q] [/D] [/E:ON|OFF|n] [/V:ON|OFF] [/F[:ON|OFF]] [/MSG] [--dialect=...] [/?]
```

| Switch | Behavior |
| :--- | :--- |
| `/P` | Permanent primary command shell (cannot terminate with `EXIT`). |
| `/C <cmd>` | Executes `<cmd>` and exits with its exit code. |
| `/K <cmd>` | Executes `<cmd>` and remains interactive. |
| `/S` | Modifies quote-stripping behavior after `/C` or `/K`. |
| `/Q` | Turns command `ECHO` off initially. |
| `/D` | Disables autorun initialization. |
| `/E:ON \| /E:OFF \| /E:<n>` | Enables/disables CMD command extensions or specifies legacy DOS environment sizing. |
| `/V:ON \| /V:OFF` | Enables/disables delayed `!VAR!` environment expansion. |
| `/F` / `/F:ON \| /F:OFF` | Auto-Fail `INT 24h` critical errors (`/F`) or toggles filename completion (`/F:ON\|OFF`). |
| `/MSG` | Keeps extended error messages resident (`WINCMD.MSG`). |
| `--dialect=<mode>` | Overrides detected shell dialect (`dos125`, `dos200`, `dos400`, `dos700`, NT, etc.). |
| `/?` | Displays startup switch help banner. |

### Internal Built-In Commands

- **Shared Cross-ISA Command Registry (`asm/inc/cmd_ids.inc`):** Defines the 47 canonical command IDs (`ASSOC`, `BREAK`, `CALL`, `CD`/`CHDIR`, `CHCP`, `CLS`, `CMD`/`COMMAND`, `COLOR`, `COPY`, `CTTY`, `DATE`, `DEL`/`ERASE`, `DIR`, `DOSKEY`, `DPATH`, `ECHO`, `ENDLOCAL`, `EXIT`, `FOR`, `FTYPE`, `GOTO`, `IF`, `KEYS`, `LH`/`LOADHIGH`, `MD`/`MKDIR`, `MKLINK`, `MODE`, `MOVE`, `PATH`, `PAUSE`, `POPD`, `PROMPT`, `PUSHD`, `RD`/`RMDIR`, `REM`, `REN`/`RENAME`, `SET`, `SETLOCAL`, `SHIFT`, `START`, `TIME`, `TITLE`, `TRUENAME`, `TYPE`, `VER`, `VERIFY`, `VOL`).
- **DOS16 Runtime Built-Ins (`asm/dos16/wincmd.asm`, `asm/dos16/cmd/m2_cmds.inc`, `asm/dos16/parse/phase34.inc`):** Active dispatcher for `VER`, `ECHO`, `SET` (including `SET /A`), `GOTO`, `SHIFT`, `IF`/`ELSE`, `FOR`, `CALL` (batch files & `:label` subroutines), `EXIT`, `CLS`, `DIR`, `TYPE`, `COPY`, `DEL`/`ERASE`, `REN`/`RENAME`, `MD`/`MKDIR`, `RD`/`RMDIR`, `CD`/`CHDIR`, `CHCP`, `TRUENAME`, `TITLE`, `VOL`, `REM`, `<`/`>`/`>>` redirection, pipelines, and `.COM`/`.EXE`/`.BAT` external execution.

---

## Win16, Win32, Win64, ARM32 & ARM64 Targets

- **Win16 (`asm/win16/`):**
  - `w16_main.asm` -> `WINCMD16.EXE`: Universal 8086 New Executable (`NE`, `expver 1.01`) targeting Windows 1.0, 2.0, 2.1x, 3.0, 3.1/3.11, and 3.2.
  - `w16_286.asm` -> `WINCMD286.EXE`: Protected-mode 80286 New Executable (`NE`, `expver 3.00`) for Windows 2.1x/286 and 3.0–3.2.
  - `wincmd_pif.asm` -> `WINCMD.PIF`: Byte-exact 369-byte Windows 1.0..3.2 WINOLDAP Program Information File.
- **Win32 (`asm/x86_32/wincmd32.asm`):** CRT-free IA-32 PE32 (`Subsystem 3.10`, `_mainCRTStartup`) importing `KERNEL32.DLL` (`GetStdHandle`, `WriteFile`, `GetCommandLineA`, `GetVersion`, `ExitProcess`) for Windows NT 3.1..11 and Windows 95/98/Me.
- **Win64 (`asm/x86_64/wincmd64.asm`):** CRT-free x86-64 PE32+ (`Subsystem 5.02`, `mainCRTStartup`) with `@proc_frame` `.pdata`/`.xdata` SEH unwind tables for Windows XP Pro x64 through Windows 11.
- **ARM32 (`asm/arm32/v4/wincmdv4.asm`, `asm/arm32/v7/wincmdv7.asm`):**
  - `WINCMD_ARMV4.EXE`: Strict ARMv4 32-bit RISC PE32 (`0x01C0`) for Windows CE / Pocket PC / Embedded with software restoring division and 5-bit shift masking.
  - `WINCMD_ARM32.EXE`: ARMv7-A Thumb-2 UAL PE32 (`0x01C4 ARMNT`, `Subsystem 6.02`) for Windows RT 8/8.1 and Windows 10 ARM32 with trap-safe `SDIV`.
- **ARM64 (`asm/arm64/wincmda64.asm`):** `WINCMD_ARM64.EXE` AArch64 PE32+ (`0xAA64`, `Subsystem 10.0`) for Windows 10 & Windows 11 on ARM with trap-safe `SDIV`, 16-byte `SP` alignment, and read-only `X18` TEB integrity.

---

## Canonical Assembly Dialect & 100% `@tag` Documentation

All source files under `asm/` use a unified canonical directive language parsed by `asm-conv` and validated by `asm-lint`:

### Canonical Directives

| Directive | Purpose |
| :--- | :--- |
| `@cpu <isa>` | Declares target CPU ISA (`8086`, `286`, `386`, `x64`, `armv4`, `armv7`, `arm64`). |
| `@format <fmt>` | Selects output container (`inc`, `com`, `com3part`, `bin16`, `coff32`, `coff64`, `armv4_pe`, `armv7_pe`, `arm64_pe`). |
| `@entry <sym>` / `@global <sym>` | Declares executable entrypoint and exported linker symbols. |
| `@extern <sym>, <proc\|dword\|qword>` | Declares external/import symbols across COFF/PE targets. |
| `@include "<path>"` | Includes shared headers (`asm/inc/*.inc`) or modular `.inc` components. |
| `@section <kind>` | Switches logical section (`resident`, `transient`, `init`, `text`, `rdata`, `data`, `bss`). |
| `@const NAME = EXPR` | Portable compile-time constant expression. |
| `@struc NAME, SIZE` / `@field name, w, n` / `@endstruc` | Byte-size-asserted structure definition verified at assemble time (`asm/inc/dos_strc.inc`, `ne_strc.inc`, `pe_strc.inc`). |
| `@proc NAME` / `@proc_frame NAME, alloc[, regs...]` / `@epilog_ret` / `@endproc` | Procedure boundaries and x64 SEH framed prolog/epilog generation. |
| `@align <pow2>` / `@data <w> <lbl\|->, ...` / `@res <w> <lbl>, <n>` | Alignment, initialized data, and zero-initialized reservations. |

### Mandatory Documentation Tags (`asm-lint` 100% Gate)

Every `.asm` / `.inc` module requires file-level and item-level `;; @tag` headers:
- **File header:** `;; @file`, `;; @isa`, `;; @desc`
- **Constants & structures:** `;; @const` / `;; @struc`, `;; @desc`
- **Procedures:** `;; @proc`, `;; @abi`, `;; @in`, `;; @out`, `;; @err`, `;; @clobbers`, `;; @desc`

`asm-lint` statically verifies that every declared `@clobbers` contract matches actual register mutations (including transitive calls) and that every control-flow path through every `@proc` preserves stack balance.

---

## Rust Verification, Emulation & Fuzzing Workspace

The Cargo workspace (`Cargo.toml`, Rust Edition 2024) contains five crates compiled with `#![deny(missing_docs)]`, `#![deny(unsafe_code)]`, `clippy::{all, pedantic, nursery}`, and denied `unwrap_used`/`expect_used`/`panic`:

| Crate | Path | Purpose & Key CLI Usage |
| :--- | :--- | :--- |
| **`wincmd-diag`** | `tools/wincmd-diag/` | Unified `ERR_*` error enumeration (`1..=511`, synchronized with `asm/inc/err_code.inc`), `CpuIsa`, `TargetAssembler`, `SourceSpan`, and the single workspace-wide `derive_more`-backed `ToolError`. |
| **`asm-conv`** | `tools/asm-conv/` | Canonical AST parser and lowering engine targeting `nasm`, `fasm`, `jwasm`, `fasmarm`, `llvm-mc`, and `armasm`.<br>`cargo run --release -p asm-conv -- --in-dir asm --out-dir dist/src`<br>`cargo run --release -p asm-conv -- --in-file <src> --out-file <dst> --target <asm>` |
| **`asm-lint`** | `tools/asm-lint/` | 100% `@tag` documentation gate, CFG stack-balance & `@clobbers`/ABI checker, ISA opcode & Win16 selector purity analyzer, cross-assembler binary equivalence checker, and footprint budget enforcer.<br>`cargo run --release -p asm-lint -- --dir asm`<br>`cargo run --release -p asm-lint -- --equiv WINCMD.COM <f1> <f2> <f3>`<br>`cargo run --release -p asm-lint -- --purity <isa> <binary>`<br>`cargo run --release -p asm-lint -- --budget <binary> <max_bytes>` |
| **`emu-cov`** | `tools/emu-cov/` | Deterministic 8086/286 CPU + MS-DOS (`1.25`, `2.00`, `4.00`, `7.10`) syscall, FCB/handle/LFN/FAT32, and EMS/XMS emulator with instruction-PC and conditional-branch edge coverage (`lcov.info`), plus M0–M2 integration tests.<br>`cargo run --release -p emu-cov -- --com build/nasm/dos16/WINCMD.COM --era 4.00 --args " /C VER" --lcov lcov.info` |
| **`diff-fuzz`** | `tools/diff-fuzz/` | Deterministic zero-dependency `SplitMix64` batch/command workload generator and cross-assembler / cross-era differential verifier.<br>`cargo run --release -p diff-fuzz -- --era 4.00 --cases 500 --seed 0x20261120 <nasm.com> <fasm.com> <jwasm.com>` |

---

## Building, Testing & Verification

### POSIX (macOS / Linux / MSYS2)

`Makefile` delegates to `scripts/build-all.sh`, which uses `scripts/bootstrap-toolchains.sh` to locate or bootstrap `NASM`, `JWasm`, `FASM`, `FASMARM`, and LLVM (`llvm-mc`, `llvm-dlltool`, `lld-link`).

```sh
# Full pipeline: static lint -> lower canonical ASM -> build all ISAs/assemblers -> verify equivalence/purity/budgets
make all

# Individual stages
make lint        # Run asm-lint over asm/ (100% doc, CFG, stack, ABI, ISA purity)
make lower       # Lower asm/ -> dist/src/<assembler>/
make build       # Assemble & link all targets across all toolchains
make verify      # Check cross-assembler equivalence, ISA purity, and size budgets
make test        # Full build + cargo test + clippy + rustdoc + 4-era emu-cov & diff-fuzz + Wine smoke tests
make fuzz        # 2,500 differential fuzz cases per DOS era (1.25, 2.00, 4.00, 7.10)
make coverage    # Run cargo llvm-cov --workspace --summary-only
make hooks       # Configure git core.hooksPath to .githooks
make clean       # Remove build/, dist/src/, and lcov.info

# Scoped builds by toolchain or target
make build TOOLCHAIN=nasm TARGET=dos16
make build TOOLCHAIN=all  TARGET=x86_64
make build TARGET=arm64
```

### Windows NT (3.1 .. 11)

`make.bat` auto-detects `%OS%=="Windows_NT"` and delegates to `BUILD.CMD`:

```cmd
make.bat           :: Lint, lower, and assemble all available toolchains
BUILD.CMD nasm     :: Assemble NASM targets only
BUILD.CMD fasm     :: Assemble FASM & FASMARM targets only
BUILD.CMD jwasm    :: Assemble JWasm targets only
BUILD.CMD hooks    :: Install .githooks via git config core.hooksPath
BUILD.CMD clean    :: Remove build\ and dist\
```

### Self-Hosting Native MS-DOS (2.00 / 4.00 / 7.x)

On real MS-DOS or a DOS VM, `make.bat` delegates to `BUILD.BAT` (strict 8.3 filenames). Canonical sources can also be lowered natively under MS-DOS using `ASMCONV.COM`:

```bat
ASMCONV /N ASM\DOS16\WINCMD.ASM DIST\SRC\NASM\DOS16\WINCMD.ASM
ASMCONV /F ASM\DOS16\WINCMD.ASM DIST\SRC\FASM\DOS16\WINCMD.ASM
ASMCONV /J ASM\DOS16\WINCMD.ASM DIST\SRC\JWASM\DOS16\WINCMD.ASM

BUILD NASM         :: Assemble WINCMD.COM and ASMCONV.COM with NASM
BUILD FASM         :: Assemble WINCMDF.COM and ASMCONVF.COM with FASM
BUILD JWASM        :: Assemble WINCMDJ.COM and ASMCONVJ.COM with JWasm
BUILD CLEAN        :: Remove built .COM artifacts
```

### Pre-Commit Quality Gate

Enable the repository hooks via `make hooks` (or `BUILD.CMD hooks`). On every commit, `.githooks/pre-commit` (POSIX `/bin/sh`, zero Python; delegates to `.githooks/pre-commit.cmd` on native Windows) runs a 6-stage verification pipeline:

1. **Line-Ending Enforcement (`dos2unix` / `unix2dos`):** Enforces `CRLF` on `.bat`/`.cmd`/`.reg`/`.ini`/`.rc`/`AUTOEXEC.BAT`/`CONFIG.SYS` and `LF` on all other text sources, skipping binaries.
2. **Static Lints:** `sh -n`, `bash -n`, `asm-lint --dir asm`, `cargo clippy --workspace --all-targets --all-features -- -D warnings`, and `RUSTDOCFLAGS="-D warnings" cargo doc --workspace --no-deps`.
3. **Multi-ISA / Multi-Assembler Build & Binary Verification:** `make build` and `make verify`.
4. **Tests, Emulation & Differential Fuzzing:** `cargo test --workspace`, 4-era (`1.25`, `2.00`, `4.00`, `7.10`) `emu-cov` & 500-case `diff-fuzz`, plus Wine smoke tests on `WINCMD32.EXE` / `WINCMD64.EXE` when `wine` is installed.
5. **Coverage Measurement:** Computes documentation coverage and `cargo llvm-cov` workspace test coverage.
6. **Badge Sync:** Dynamically updates the `doc coverage` and `test coverage` Shields.io badges at the top of `README.md`.

---

## Continuous Integration (`c-ci`-Inspired Pipeline)

The CI pipeline (`.github/workflows/` + `ci/`) adopts the reusable-workflow, sparse multi-platform matrix, container verification, local replication, and real headless QEMU MS-DOS testing architecture of [SamuelMarks/c-ci](https://github.com/SamuelMarks/c-ci):

- **Reusable & Caller GitHub Actions (`.github/workflows/asm-ci.yml`, `.github/workflows/ci.yml`):**
  1. **Static & EOL Gate (`static-lint-and-eol`):** Enforces repository CRLF/LF rules (`ci/check_eol.sh`), shell/Ruby syntax (`sh -n`, `bash -n`, `ruby -c`), `asm-lint --dir asm` (100% `@tag` docs, CFG stack balance, ABI/clobbers, ISA purity), `cargo clippy -- -D warnings`, and `cargo doc` (`-D warnings`).
  2. **Cross-OS / Multi-Assembler / Multi-ISA Matrix (`build-and-verify-matrix`):**
     - **Linux x64 (`ubuntu-latest`):** All 6 assemblers (`NASM`, `FASM`, `JWasm`, `FASMARM`, `LLVM-MC`, `lld-link`) across all 6 targets + Wine smoke tests + binary artifact uploads.
     - **Linux ARM64 (`ubuntu-24.04-arm`, experimental):** `NASM`, `JWasm`, `LLVM` across all 6 targets.
     - **macOS Apple Silicon (`macos-latest`):** Native `NASM`, `JWasm`, Homebrew `LLVM` across all 6 targets.
     - **Windows 2025 & Windows 2022 (`windows-2025`, `windows-2022`):** WerFault/AeDebug suppression, native Windows `NASM`, `FASM`, `JWasm`, `FASMARM`, `LLVM`, 3-way equivalence verification, and **real Windows NT kernel execution** of `WINCMD32.EXE` and `WINCMD64.EXE`.
     - **Windows Cygwin (`windows-2025` + `cygwin`):** `SHELLOPTS=igncr` POSIX-on-Windows `NASM` verification.
     - **Windows 11 ARM64 (`windows-11-arm`, experimental):** Native `WINCMD_ARM64.EXE` build and execution on real ARM64 hardware.
  3. **4-Era Emulation & Differential Fuzzing (`dos-emu-and-fuzz-matrix`):** Parallel jobs for MS-DOS `1.25`, `2.00`, `4.00`, and `7.10` running `emu-cov` (with per-era LCOV artifacts), self-hosted `ASMCONV.COM` smoke tests, and `diff-fuzz` (`NASM == FASM == JWasm`).
  4. **Containerized Distro Matrix (`docker-container-matrix`):** Parallel builds & tests inside **Ubuntu 24.04 (glibc)** (`ci/ubuntu.Dockerfile`) and **Alpine Linux (musl)** (`ci/alpine.Dockerfile`).
  5. **Coverage & Footprint Summary (`coverage-and-summary`):** `cargo llvm-cov --workspace` LCOV + formatted `$GITHUB_STEP_SUMMARY` with doc coverage and exact byte sizes of every built artifact.
  6. **Opt-In Real Headless QEMU MS-DOS & FreeDOS (`msdos-qemu-test`):** Toggled via `msdos_qemu_testing: true` (or `workflow_dispatch`), stages `SamuelMarks/bento` DOS disk images, boots headless QEMU **MS-DOS 1.25**, **MS-DOS 2.00**, **MS-DOS 4.00**, and **FreeDOS 1.3**, uploads `WINCMD.COM`, `WINCMD.MSG`, and `ASMCONV.COM` over COM1 (`SERGET.COM`), and executes live commands inside real DOS.
- **Local CI Replication (`ci/replicate_ci.sh`, `ci/replicate_ci.cmd`, `Makefile`):**
  ```sh
  make ci-list       # List all local CI stages
  make ci-local      # Run lint + native multi-ISA build/verify + 4-era emu-fuzz + coverage
  make ci-docker     # Verify inside Ubuntu (glibc) and Alpine (musl) containers
  make ci-dos-qemu   # Run live headless QEMU MS-DOS 1.25/2.00/4.00 & FreeDOS 1.3 via Bento
  ```

---

## Repository Layout

```text
win-cmd-asm/
├── .github/
│   └── workflows/            # Reusable asm-ci.yml and caller ci.yml GitHub Actions pipelines
├── ci/                       # Dockerfiles (ubuntu/alpine), EOL check, Bento QEMU DOS harness & local replicators
├── asm/
│   ├── inc/                  # Shared cross-ISA equates, ERR_* (0..511), cmd IDs, AST tags, DOS/NE/PE structs
│   ├── tc/                   # Assembler shims & import defs (nasm/, fasm/, jwasm/, arm_ms/)
│   ├── dos16/                # 8086 3-part WINCMD.COM, WINCMD.MSG, and self-hosted ASMCONV.COM
│   │   ├── res/              # Resident ISRs (INT 23h/24h), DOS 1.25 loader/redirector, EXEC, EMS/XMS reloader
│   │   ├── init/             # Cold-boot DOS 1.25..8.0, WINOLDAP, BIOS/TTY, EMS/XMS & switch detector
│   │   ├── hal/              # Era HALs: k_dos125.inc, k_dos200.inc, k_dos400.inc, k_dos700.inc
│   │   ├── core/             # Arena, CRC-16, DBCS/NLS, bounded strings, trap-free 32/64-bit math, err output
│   │   ├── read/             # Dual BIOS/TTY line editor and stateless BOM/EOF/caret batch reader
│   │   ├── parse/            # 4-phase expander, modifiers, env var ops, lexer, recursive-descent Arena AST
│   │   ├── rt/               # Precedence-climbing 32-bit signed SET /A evaluator
│   │   ├── cmd/              # Era-dispatching VFS built-ins, redirection, FILES=8 batch & child execution
│   │   └── util/             # Self-hosted 8086 ASMCONV.COM canonical assembly lowerer
│   ├── win16/                # Universal 8086 NE (w16_main.asm), 80286 NE (w16_286.asm), WINCMD.PIF
│   ├── x86_32/               # CRT-free IA-32 PE32 (Subsystem 3.10) shell (wincmd32.asm)
│   ├── x86_64/               # CRT-free x86-64 PE32+ (Subsystem 5.02, SEH) shell (wincmd64.asm)
│   ├── arm32/                # ARMv4 WinCE PE32 (v4/wincmdv4.asm) & ARMv7-A Thumb-2 ARMNT (v7/wincmdv7.asm)
│   └── arm64/                # AArch64 PE32+ (Subsystem 10.0, X18-safe) shell (wincmda64.asm)
├── tools/                    # Zero-unsafe Rust 2024 verification & lowering workspace
│   ├── wincmd-diag/          # Unified ErrCode (1..=511), ISA/assembler types, ToolError
│   ├── asm-conv/             # Canonical assembly parser & 6-assembler lowering engine
│   ├── asm-lint/             # 100% doc gate, CFG stack/ABI/clobber checker, purity, equivalence & budget gates
│   ├── emu-cov/              # Deterministic 8086/286 + 4-era DOS emulator, LCOV reporter & integration tests
│   └── diff-fuzz/            # SplitMix64 workload generator & cross-assembler differential fuzzer
├── scripts/                  # POSIX build-all.sh and multi-assembler bootstrap-toolchains.sh
├── .githooks/                # POSIX (pre-commit) and Windows (pre-commit.cmd) 6-stage quality hooks
├── Makefile                  # GNU Make orchestrator (macOS / Linux / MSYS2)
├── make.bat                  # Universal Windows NT / MS-DOS build entrypoint
├── BUILD.CMD / BUILD.BAT     # Native Windows NT and strict-8.3 MS-DOS build scripts
└── Cargo.toml                # Workspace manifest and strict lint configuration
```

---

## License

Dual-licensed under **MIT** or **Apache-2.0** (matching `Cargo.toml`).
