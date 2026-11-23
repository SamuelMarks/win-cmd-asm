# `win-cmd-asm` Architecture Reference

This document provides a comprehensive systems-engineering specification of **`win-cmd-asm`**: a 100% CRT-free, canonical multi-ISA, multi-assembler implementation of MS-DOS `COMMAND.COM`, Windows Command Prompt (`CMD.EXE`), an embedded 16-bit virtual 8086 execution engine (`vm8086`), 19 standalone companion command-line utilities (`95` binaries across `5` ISA families), and the six-crate zero-`unsafe` Rust lowering, static-analysis, emulation, differential-fuzzing, and media-mastering suite (`tools/`).

---

## Table of Contents

1. [System Overview & Core Invariants](#1-system-overview--core-invariants)
2. [End-to-End Pipeline Architecture](#2-end-to-end-pipeline-architecture)
3. [Multi-ISA & Binary Format Target Matrix](#3-multi-isa--binary-format-target-matrix)
4. [Canonical Assembly Dialect & 6-Backend Lowering (`asm-conv` & `ASMCONV.COM`)](#4-canonical-assembly-dialect--6-backend-lowering-asm-conv--asmconvcom)
5. [Static Verification, Purity & Equivalence Gates (`asm-lint`)](#5-static-verification-purity--equivalence-gates-asm-lint)
6. [Unified Error Taxonomy (`err_code.inc` & `wincmd-diag`)](#6-unified-error-taxonomy-err_codeinc--wincmd-diag)
7. [Target Platform Architectures & OS HALs](#7-target-platform-architectures--os-hals)
   - [7.1 `dos16`: 3-Part MS-DOS 1.25 / 2.00 / 4.00 / 7.x Architecture](#71-dos16-3-part-ms-dos-125--200--400--7x-architecture)
   - [7.2 `win16`: Real & 80286 Protected-Mode `NE` Architecture](#72-win16-real--80286-protected-mode-ne-architecture)
   - [7.3 `x86_32`: CRT-Free `IA-32` `PE32` (`Subsystem 3.10`) Architecture](#73-x86_32-crt-free-ia-32-pe32-subsystem-310-architecture)
   - [7.4 `x86_64`: CRT-Free `AMD64` `PE32+` (`Subsystem 5.02`) Architecture](#74-x86_64-crt-free-amd64-pe32-subsystem-502-architecture)
   - [7.5 `arm32`: `ARMv4` WinCE (`0x01C0`) & `ARMv7-A` Thumb-2 `ARMNT` (`0x01C4`)](#75-arm32-armv4-wince-0x01c0--armv7-a-thumb-2-armnt-0x01c4)
   - [7.6 `arm64`: `AArch64` `PE32+` (`0xAA64`, `Subsystem 10.0`) Architecture](#76-arm64-aarch64-pe32-0xaa64-subsystem-100-architecture)
8. [Shell Language, Reader, 5-Phase Expander & AST Execution Engine](#8-shell-language-reader-5-phase-expander--ast-execution-engine)
   - [8.1 Stateless Byte-Offset Batch & Interactive Reader](#81-stateless-byte-offset-batch--interactive-reader)
   - [8.2 5-Phase Variable, Parameter & Modifier Expansion](#82-5-phase-variable-parameter--modifier-expansion)
   - [8.3 Bump-Arena Lexer & Compact AST (`ast_kind.inc`)](#83-bump-arena-lexer--compact-ast-ast_kindinc)
   - [8.4 11-Tier Pratt `SET /A` Arithmetic Engine](#84-11-tier-pratt-set-a-arithmetic-engine)
   - [8.5 Redirection, Concurrent `CreatePipe` & Spooled Pipelines](#85-redirection-concurrent-createpipe--spooled-pipelines)
   - [8.6 Built-In Command Dispatcher (`cmd_ids.inc`)](#86-built-in-command-dispatcher-cmd_idsinc)
9. [Embedded Multi-ISA `vm8086` 16-Bit `.COM` & `MZ` Virtual Executor](#9-embedded-multi-isa-vm8086-16-bit-com--mz-virtual-executor)
10. [Standalone 19-Utility Companion Suite Architecture](#10-standalone-19-utility-companion-suite-architecture)
11. [Deterministic 5-ISA Emulation, Differential Fuzzing & Media Mastering](#11-deterministic-5-isa-emulation-differential-fuzzing--media-mastering)

---

## 1. System Overview & Core Invariants

`win-cmd-asm` is engineered around seven non-negotiable architectural invariants:

1. **Zero C Runtime (`msvcrt` / `ucrt` / `libc`)**: Every executable across all five CPU ISAs links with `/NODEFAULTLIB` (or emits flat/PE/NE headers directly from assembly) and defines its own raw OS loader entrypoint (`res_entry` / `res1_boot` at `ORG 0x0100`, `mz_entry`, `w16_entry` / `w16_entry_286`, or `mainCRTStartup`). Memory management uses deterministic OS primitives (`INT 21h AH=48h/4Ah`, `GlobalAlloc`/`LocalAlloc`, or `VirtualAlloc`/`HeapAlloc`) paired with per-statement O(1) bump-pointer arenas.
2. **Single Canonical Assembly Source (`asm/`)**: All `239` canonical `.asm`/`.inc` modules are authored once in a strict `@`-directive canonical dialect and lowered deterministically into six assembler syntaxes (`NASM`, `FASM`, `JWasm`, `FASMARM`, `LLVM llvm-mc`, and `Microsoft armasm`/`armasm64`) without preprocessor spaghetti or duplicated source trees.
3. **Strict FAT 8.3 Self-Hosting Hygiene**: Every directory and filename under `asm/dos16/`, `asm/win16/`, and `asm/inc/` obeys `<= 8.3` naming and path depth `<= 3` (verified by `asm-lint`), enabling native self-hosting inside MS-DOS `2.00`/`4.00` via `BUILD.BAT` and `ASMCONV.COM`.
4. **Hardware & ABI Purity Across 44 Years of Silicon**:
   - `dos16` and `WINCMD16.EXE` emit strictly pure `8086` instructions (zero `186+` `PUSHA`, `SHL reg, imm8`, `IMUL imm`, or `Jcc rel16`).
   - `WINCMD286.EXE` obeys strict `80286` Protected-Mode selector rules (zero segment arithmetic; `INT 31h` / `AllocDStoCSAlias` / `SelectorAccessRights` only).
   - `WINCMD32.EXE` targets baseline `i386` (`Subsystem 3.10`, zero `CMOVcc` / SSE2) so a single binary runs from Windows NT `3.1` and Windows `95` through Windows `11` WOW64.
   - `WINCMD64.EXE` provides full `.pdata`/`.xdata` unwind metadata (`@proc_frame`), `16`-byte stack alignment before every `CALL`, `HIGH_ENTROPY_VA`, `GUARD_CF` (`CFG`), and Intel `CET` compatibility.
   - `WINCMD_ARMV4.EXE` emits strict 32-bit `ARMv4` (`0x01C0`, zero `BLX`, zero `MOVW`/`MOVT`, zero hardware `UDIV`/`SDIV`, byte-safe unaligned loads/stores), whereas `WINCMD_ARM32.EXE` emits `ARMv7-A` Thumb-2 `UAL` (`0x01C4` `ARMNT`, strict `IT` blocks `<= 4` instructions, `.pdata` unwinds).
   - `WINCMD_ARM64.EXE` obeys Microsoft ARM64 ABI (`0xAA64`), **never mutates `X18`** (reserved for user-mode `TEB`), maintains hardware `16`-byte `SP` alignment at every load/store, invokes `__chkstk` (`X15 = bytes / 16`) for large frames, and wraps non-leaf frames in `HINT #25` (`PACIASP`) / `HINT #29` (`AUTIASP`) + `BTI c`.
5. **Internal UTF-8 / WTF-8 Processing on Win32/Win64/ARM**: Rather than duplicating string, lexer, and parser logic for ANSI and UTF-16LE, all 32/64-bit PE targets transcode OS `UTF-16LE` strings (preserving unpaired surrogates via **WTF-8** so arbitrary NTFS paths round-trip losslessly!) at the HAL boundary.
6. **Zero-Open-Handle Batch Execution**: Batch files are read via stateless byte offsets (`open -> seek -> read logical line -> close`) before executing any command, matching `COMMAND.COM`/`CMD.EXE` semantics on self-modifying scripts and fitting inside MS-DOS `2.00`'s `FILES=8` system table.
7. **Deterministic Verification & Reproducible Packaging**: All Rust host tools forbid `unsafe`, `unwrap()`, `expect()`, `panic!()`, and `anyhow`, enforce `100%` Rust and assembly `@tag` documentation coverage, and master bit-for-bit reproducible `.IMG`, `.ISO`, `.ZIP`, and standalone source `.tar.gz` artifacts (`SOURCE_DATE_EPOCH = 1980-01-01 00:00:00 UTC`).

---

## 2. End-to-End Pipeline Architecture

```text
                        ┌──────────────────────────────────────────┐
                        │     Canonical Assembly Tree (asm/)       │
                        │ 239 .asm/.inc modules + 26 asm/tc/ shims │
                        └────────────────────┬─────────────────────┘
                                             │
                     ┌───────────────────────┴───────────────────────┐
                     ▼                                               ▼
       ┌───────────────────────────┐                   ┌───────────────────────────┐
       │      tools/asm-lint       │                   │      tools/asm-conv       │
       │  - 100% ;; @tag doc gate  │                   │  (or DOS16 ASMCONV.COM)   │
       │  - FAT 8.3 path gate      │                   │  Lossless AST Lowering    │
       │  - Per-proc CFG & stack   │                   └─────────────┬─────────────┘
       │  - Transitive @clobbers   │                                 │
       │  - ABI callee-save check  │                                 ▼
       └───────────────────────────┘     ┌───────────────────────────────────────────────────────┐
                                         │              dist/src/<backend>/                      │
                                         │  nasm | fasm | fasm-pe32 | fasm-coff | jwasm          │
                                         │  fasmarm | llvm-mc | armasm                           │
                                         └───────────────────────────┬───────────────────────────┘
                                                                     │
                       ┌─────────────────────────────────────────────┼──────────────────────────────────────────┐
                       ▼                                             ▼                                          ▼
         ┌───────────────────────────┐                 ┌───────────────────────────┐              ┌───────────────────────────┐
         │   x86-16 DOS / Win16      │                 │   IA-32 & x86-64 PE/PE+   │              │  ARMv4 / ARMv7 / ARM64 PE │
         │ NASM / FASM / JWasm       │                 │ NASM / FASM / JWasm       │              │ FASMARM / LLVM-MC         │
         │ (+ wlink optional)        │                 │ + lld-link (or direct PE) │              │ + lld-link (or direct PE) │
         └─────────────┬─────────────┘                 └─────────────┬─────────────┘              └─────────────┬─────────────┘
                       └─────────────────────────────────────────────┼──────────────────────────────────────────┘
                                                                     ▼
                                     ┌───────────────────────────────────────────────────────────────┐
                                     │                Post-Build Verification Gates                  │
                                     │  1. asm-lint --equiv   (Cross-assembler byte/.text parity)    │
                                     │  2. asm-lint --purity  (8086/286/386/x64/ARMv4/v7/64 decode)  │
                                     │  3. asm-lint --budget  (Hard RAM & disk byte limits)          │
                                     │  4. tools/emu-cov      (5-ISA VMs + LCOV branch-edge trace)   │
                                     │  5. tools/diff-fuzz    (10,000+ 7-ISA + live cmd.exe cases)   │
                                     │  6. QEMU DOS Matrix    (MS-DOS 1.25, 2.00, 4.00, FreeDOS 1.3) │
                                     └───────────────────────────────┬───────────────────────────────┘
                                                                     ▼
                                     ┌───────────────────────────────────────────────────────────────┐
                                     │                   tools/media-pkg (dist/release/)             │
                                     │  160K/360K/720K/1.44M .IMG | Universal .ISO | 5 .ZIPs         │
                                     │  3 Zero-Rust Source .tar.gz (NASM/FASM/JWasm) | SHA256SUMS    │
                                     └───────────────────────────────────────────────────────────────┘
```

---

## 3. Multi-ISA & Binary Format Target Matrix

| Target | Source Entrypoint | `@cpu` / `@format` | Machine / Subsys | Security & Loader Features | Primary Output |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **DOS Full Shell** | `asm/dos16/wincmd.asm` | `8086` / `com3part` | Flat `.COM` (`ORG 100h`) | 3-part `RESIDENT` + `TRANSIENT` (`CRC-16`) + `INIT`; `EMS 4.0` swap | `WINCMD.COM` (`35,632 B`) |
| **DOS 1.25 Tiny Shell** | `asm/dos16/wincmd1.asm` | `8086` / `com3part` | Flat `.COM` (`ORG 100h`) | Compact FCB-only 3-part shell (`<= 20 KiB` budget) | `WINCMD1.COM` (`1,088 B`) |
| **DOS Relocatable MZ** | `asm/dos16/wincmdmz.asm` | `8086` / `mz16` | 16-bit `MZ` (`5A4Dh`) | Segment fixup table, dedicated `MZ_STACK`, FASM `MZ` / OMF link | `WINCMDMZ.EXE` (`176–560 B`) |
| **DOS Lowerer** | `asm/dos16/util/asmconv.asm` | `8086` / `com` | Flat `.COM` (`ORG 100h`) | In-DOS canonical -> `NASM`/`FASM`/`JWasm` self-host lowerer | `ASMCONV.COM` (`1,375 B`) |
| **DOS `/MSG` Table** | `asm/dos16/wincmd_m.asm` | `8086` / `bin16` | `"WMSG"` indexed bin | External DOS `4.00+` extended error string table | `WINCMD.MSG` (`169 B`) |
| **Win16 Universal NE** | `asm/win16/w16_main.asm` | `8086` / `bin16` (`NE`) | `NE` (`454Eh`), Win `1.0+` | Multi-segment (`_TEXT`, `DGROUP`), Real & PM clean, GDI + DBCS | `WINCMD16.EXE` (`11,264 B`) |
| **Win16 286 PM NE** | `asm/win16/w16_286.asm` | `286` / `bin16` (`NE`) | `NE` (`454Eh`), `.286p` | Adds Segment 3 (`_TRN286` `MOVEABLE \| DISCARDABLE`), `INTERNALREF` | `WINCMD286.EXE` (`11,776 B`) |
| **Win16 PIF Profile** | `asm/win16/w16_pif.asm` | `8086` / `bin16` | `WINOLDAP` `.PIF` | `369 B` classic + `WINDOWS 286 3.0` + `WINDOWS 386 3.0` sections | `WINCMD.PIF` (`545 B`) |
| **Win32 IA-32 Shell** | `asm/x86_32/wincmd32.asm` | `386` / `coff32` | `PE32` `0x014C`, `3.10` | `/SAFESEH`, `DYNAMIC_BASE`, `NX_COMPAT`, `*W`/`*A` HAL, `vm8086` | `WINCMD32.EXE` (`56,832 B`) |
| **Win64 AMD64 Shell** | `asm/x86_64/wincmd64.asm` | `x64` / `coff64` | `PE32+` `0x8664`, `5.02` | `.pdata`/`.xdata` SEH, `HIGH_ENTROPY_VA`, `GUARD_CF`, `CET`, `vm8086` | `WINCMD64.EXE` (`69,632 B`) |
| **WinCE ARMv4 Shell** | `asm/arm32/v4/wincmdv4.asm` | `armv4` / `armv4_pe` | `PE32` `0x01C0`, `9.0` | `COREDLL.DLL` imports, strict 4B alignment, soft-div, `vm8086` | `WINCMD_ARMV4.EXE` (`65,024 B`) |
| **WinRT ARMv7 Shell** | `asm/arm32/v7/wincmdv7.asm` | `armv7` / `armv7_pe` | `PE32` `0x01C4`, `6.02` | Thumb-2 `UAL`, `.pdata`, `DYNAMIC_BASE`, `NX_COMPAT`, `GUARD_CF`, `vm8086` | `WINCMD_ARM32.EXE` (`50,688 B`) |
| **Win10/11 ARM64 Shell** | `asm/arm64/wincmda64.asm` | `arm64` / `arm64_pe` | `PE32+` `0xAA64`, `10.0` | `X18` `TEB` safe, `PAC`/`BTI`, `.pdata`, `HIGH_ENTROPY_VA`, `GUARD_CF`, `vm8086` | `WINCMD_ARM64.EXE` (`69,120 B`) |
| **95 Companion Tools** | `asm/{dos16,x86_32,x86_64,arm32,arm64}/util/*.asm` | Per-ISA | `.COM` / `PE32` / `PE32+` | CRT-free standalone `ATTRIB`..`WHOAMI` (`19` × `5` targets) | `19` `.COM` + `76` `.EXE` |

---

## 4. Canonical Assembly Dialect & 6-Backend Lowering (`asm-conv` & `ASMCONV.COM`)

Assemblers across the x86 and ARM ecosystems disagree fundamentally on memory-vs-immediate operand syntax, local label scoping, structure layout assertions, and unwind-table generation. `tools/asm-conv` (`src/{parser.rs,ast.rs,lower.rs}`) parses canonical `.asm`/`.inc` files into a typed `CanonicalModule` AST and lowers them into backend-specific trees under `dist/src/`:

### 4.1 Canonical Directives Grammar

| Canonical Construct | Purpose | Backend Lowering Summary |
| :--- | :--- | :--- |
| `@cpu 8086\|286\|386\|x64\|armv4\|armv7\|arm64` | Selects hardware ISA & activates strict instruction filters | NASM `CPU 8086`/`386`/`X64`; FASM `include '8086.inc'` / `use32` / `use64`; JWasm `.8086` / `.286p` / `.386` / `.x64`; LLVM `.arch armv4t` / `.thumb` + `.syntax unified` / `.text` |
| `@format com\|com3part\|mz16\|bin16\|coff32\|coff64\|armv4_pe\|armv7_pe\|arm64_pe\|inc` | Selects binary container, segments, or COFF/PE headers | Emits 3-part virtual-origin segments (`com3part`), relocatable `MZ` (`mz16`), COFF sections (`coff32`/`coff64`), or FASM direct `PE`/`PE64`/`ARMPE`/`ARM64PE` headers + import/unwind directories |
| `@section text\|data\|rdata\|bss\|resident\|transient\|init` | Opens logical section with target alignment | Maps to `.text`/`.data`/`.rdata`/`.bss` or paragraph-aligned (`ALIGN 16`) 16-bit segments |
| `@entry <sym>`, `@global <sym>`, `@extern <sym>, <type>`, `@safeseh <sym>` | Symbol visibility, entrypoint, IAT imports, `/SAFESEH` registration | Emits `GLOBAL`/`EXTERN`/`safeseh` (NASM), `public`/`extrn` or direct PE IAT thunks (FASM/FASMARM), `PUBLIC`/`EXTERNDEF` + `.SAFESEH` (JWasm), `.globl` (LLVM-MC) |
| `@const NAME = expr` | Assemble-time constant | `NAME equ expr` (NASM/FASM/JWasm/FASMARM/ARMASM) or `.equ NAME, expr` (LLVM-MC) |
| `@struc NAME, EXPECTED_SIZE` ... `@field f, byte\|word\|dword\|qword, count` ... `@endstruc` | Typed structure layout **with static byte-size assertion** | NASM `STRUC` + `%if NAME_size != EXPECTED_SIZE %error`; FASM `virtual at 0` + `assert sizeof.NAME = EXPECTED_SIZE`; JWasm `STRUCT` + `.ERRNZ SIZEOF NAME - EXPECTED_SIZE`; ARM offset `.equ` constants |
| `@proc <name>` ... `@endproc` | Standard procedure boundary & local-label scope | Emits symbol label; rewrites `.local` labels inside JWasm `PROC ... ENDP` to `@@local` under `OPTION CASEMAP:NONE` |
| `@proc_frame <name>, <alloc>, [regs...]` ... `@epilog_ret` ... `@endproc` | Win64 `.pdata`/`.xdata` SEH framed procedure | NASM macro prologue + `.pdata`/`.xdata` `UNWIND_INFO`; JWasm native `PROC FRAME` (`.pushreg` / `.allocstack` / `.endprolog`); FASM `PE64` / `fasm-coff` explicit unwind records |
| `@data db\|dw\|dd\|dq <name\|->, <items...>` | Initialized data (with `-` anonymous continuation) | Separates `label` declaration in FASM so `.local` label parents never attach to data symbols; emits `.byte`/`.hword`/`.word`/`.quad` on LLVM-MC |
| `@res byte\|word\|dword\|qword <name>, <count>` | Uninitialized reservation (`.bss` or scratch) | `resb`/`resw`/`resd`/`resq` (NASM), `rb`/`rw`/`rd`/`rq` (FASM/FASMARM), `DB count DUP (?)` (JWasm), `.space` (LLVM-MC) |
| `@align <N>` | Code (`NOP`-padded) or data alignment | `ALIGN N` / `align N` / `.balign N` |

### 4.2 Resolving the x86 `OFFSET` vs `[mem]` Ambiguity & ARM Pseudo-Ops
- **x86 Address vs. Memory Loads**: In NASM and FASM, `mov si, msg` loads an immediate offset while `mov ax, word [var]` loads memory; in MASM/JWasm, `mov si, var` performs a memory load if `var` is a data label unless prefixed with `OFFSET`! Canonical x86 source mandates explicit `addr(sym)` for immediate symbol addresses and explicit `byte [...]` / `word [...]` / `dword [...]` / `qword [...]` brackets for memory loads. `asm-conv` lowers `addr(sym + k)` to `sym + k` on NASM/FASM and `OFFSET sym + k` on JWasm, and rewrites `[cs:off]` to `CS:[off]` with explicit `PTR` qualifiers on JWasm.
- **Direct Linker-Free FASM Modes (`fasm-pe32`, `fasm` `PE64`, `fasmarm`)**: When lowering `x86_32`, `x86_64`, `arm32`, and `arm64`, `asm-conv` walks transitive `@include` trees to collect all `@extern __imp_*` imports and `@proc_frame` / ARM `@proc` unwind spans, emitting self-contained FASM/FASMARM sources that assemble directly into valid `PE32`/`PE32+` executables (complete with `.idata`, `.reloc`, and `.pdata`/`.xdata`) **with zero external linker**. Simultaneously, `fasm` (`x86_32` MS COFF) and `fasm-coff` (`x86_64` MS64 COFF) trees are generated to link alongside NASM, JWasm, and `llvm-mc` via `lld-link`.
- **Self-Hosting 8086 Lowerer (`ASMCONV.COM`)**: `asm/dos16/util/asmconv.asm` (`1,375` bytes) implements the 16-bit subset of canonical lowering in pure 8086 assembly (`ASMCONV /N|/F|/J IN.ASM OUT.ASM`), allowing a vintage MS-DOS `2.00`/`4.00` machine with zero Rust toolchain to regenerate NASM, FASM, or JWasm sources natively.

---

## 5. Static Verification, Purity & Equivalence Gates (`asm-lint`)

`tools/asm-lint` (`2,976` lines of Rust) enforces six compile-time and post-link verification layers:

1. **100% Structured `@tag` Documentation Gate**:
   - Every file must declare `;; @file`, `;; @isa`, and `;; @desc`.
   - Every `@proc` / `@proc_frame` must declare `;; @proc`, `;; @abi` (`dos16`, `win16`, `win32`, `win64_ms`, `wince_arm`, `win_armnt`, or `win_arm64`), `;; @in`, `;; @out`, `;; @err`, `;; @clobbers`, and `;; @desc`.
   - Every `@struc` and `@const` must carry `;; @struc`/`;; @const` and `;; @desc`.
2. **FAT 8.3 Path & Depth Gate**:
   - Verifies that every path under `asm/dos16/`, `asm/win16/`, and `asm/inc/` has `<= 3` components and `<= 8.3` ASCII alphanumeric/`_`/`-` names.
3. **Per-Procedure Control-Flow Graph (CFG) & Stack-Balance Analyzer**:
   - Builds a basic-block CFG across unconditional jumps (`jmp`, `b`), conditional branches (`jcc`, `loop*`, `jcxz`, `cbz`/`cbnz`, `tbz`/`tbnz`, `b.cond`), and terminators (`ret`, `retf`, `iret`, `@epilog_ret`).
   - Runs fixed-point dataflow tracking stack pointer deltas (`push`/`pop`, `enter`/`leave`, `mov sp, bp`, `add`/`sub sp`, `stp`/`ldp [sp, #imm]!`) and rejects any procedure with conflicting merge-point stack depths, non-zero exit delta, or Win64/ARM64 `16`-byte stack misalignment at `call`/`bl` sites.
4. **Transitive `@clobbers` & Calling-Convention Verifier**:
   - Tracks direct register writes, implicit instruction clobbers (`mul`/`div`/`cwd`/`cdq`/`cqo`/`lodsb`/`stosb`/`movsb`/`cpuid`/`syscall`), and transitive `call`/`bl` targets across `@include` graphs, verifying both that `@clobbers` is complete and that ABI callee-saved registers (`ebx`/`esi`/`edi`/`ebp` on `win32`; `rbx`/`rbp`/`rdi`/`rsi`/`r12`–`r15` on `win64_ms`; `r4`–`r11` on ARM32; `x18` + `x19`–`x29` on `win_arm64`) are preserved.
5. **Post-Link Machine-Code Disassembler Purity Gate (`--purity`)**:
   - Disassembles flat `.COM`, `MZ`, `NE` code segments, and `PE32`/`PE32+` `.text` sections to reject out-of-tier opcodes (`186+` in `8086` targets; segment arithmetic in `286` Win16 PM; `686+`/SSE in `386` PE32; missing `.pdata` or `X18` writes in `x64`/`arm64`; `ARMv5+` or unaligned word accesses in `armv4`; malformed `IT` blocks in `armv7`).
6. **Cross-Assembler Equivalence (`--equiv`) & Byte Budgets (`--budget`)**:
   - Asserts exact byte-for-byte identity across `NASM`, `FASM`, and `JWasm` on all flat `.COM`, `.MSG`, `.PIF`, and Win16 `NE` binaries, and normalized `.text` code-section equivalence across `NASM`, `FASM`, `FASM-PE32`/`FASM-COFF`, `JWasm`, `FASMARM`, and `LLVM` on `MZ`/`PE32`/`PE32+` executables, while enforcing hard byte ceilings on every binary and floppy bundle.

---

## 6. Unified Error Taxonomy (`err_code.inc` & `wincmd-diag`)

Both the assembly targets (`asm/inc/err_code.inc`) and the Rust workspace (`tools/wincmd-diag/src/lib.rs`) share a single canonical 16-bit error code space (`0..=511`):

- **`0x0000` (`ERR_OK`)**: Success.
- **`0x0001..=0x005F` (Canonical MS-DOS / Win32 Kernel Errors)**: Direct 1:1 mapping with `INT 21h` / `GetLastError()` codes (`ERR_INVALID_FUNCTION = 0x01`, `ERR_FILE_NOT_FOUND = 0x02`, `ERR_PATH_NOT_FOUND = 0x03`, `ERR_TOO_MANY_OPEN_FILES = 0x04`, `ERR_ACCESS_DENIED = 0x05`, `ERR_INVALID_HANDLE = 0x06`, `ERR_OUT_OF_MEMORY = 0x08`, `ERR_BAD_EXE_FORMAT = 0x0B`, `ERR_INVALID_DRIVE = 0x0F`, `ERR_NO_MORE_FILES = 0x12`, `ERR_WRITE_PROTECT = 0x13`, `ERR_NOT_READY = 0x15`, `ERR_CRC = 0x17`, `ERR_SHARING_VIOLATION = 0x20`, `ERR_DISK_FULL = 0x27`, `ERR_FILE_EXISTS = 0x50`, `ERR_FAIL_INT24 = 0x53`).
- **`0x0100..=0x01FF` (Shell Syntax, Parser, `SET /A` & Execution Errors)**: Shell-specific diagnostics (`ERR_SYNTAX` `"The syntax of the command is incorrect."`, `ERR_BAD_CMD` `"'.' is not recognized as an internal or external command..."` / `"Bad command or file name"`, `ERR_DIV_ZERO`, `ERR_UNBALANCED_PAREN`, `ERR_INVALID_NUMBER`, `ERR_MISSING_OPERAND`, `ERR_LABEL_NOT_FOUND`, `ERR_MAX_RECURSION`).
- **`ToolError` (`wincmd-diag`)**: A single workspace-wide Rust error enum derived via `derive_more::{Display, Error, From}` covering `Io`, `CliUsage`, `Syntax`, `DocMissing`, `CfgStack`, `ClobberAbi`, `Purity`, `Equivalence`, `Budget`, `Emulator`, `DiffFuzz`, and `MediaPackage`.

---

## 7. Target Platform Architectures & OS HALs

### 7.1 `dos16`: 3-Part MS-DOS 1.25 / 2.00 / 4.00 / 7.x Architecture

`WINCMD.COM` (`asm/dos16/wincmd.asm`, `35,632` bytes) reproduces the classic MS-DOS three-segment memory split inside a single `.COM` image so child applications receive almost the entire `640 KiB` conventional memory arena:

```text
Conventional Memory (00000h .. 9FFFFh):
┌──────────────────────────────────────────────────────────────────────────┐
│ PSP (256 B) + RESIDENT (<= 2,816 B low RAM, shrunk via INT 21h AH=4Ah)   │
│  - "WINCMD\x1A\x01" IPC signature & RES_STATE                            │
│  - INT 22h / INT 23h (Ctrl+C) / INT 24h (Critical Error) ISRs            │
│  - ldr_125 (DOS 1.25 .COM loader), red_125 (DOS 1.25 I/O hook)           │
│  - exec_200 (DOS 2.00+ SS:SP-preserving AH=4Bh trampoline)               │
│  - ems_40 (LIM EMS 4.0 INT 67h fast transient swap)                      │
├──────────────────────────────────────────────────────────────────────────┤
│                        FREE CONVENTIONAL MEMORY                          │
│          (Available to external .COM / .EXE child applications)          │
├──────────────────────────────────────────────────────────────────────────┤
│ TRANSIENT at wTrnSeg:0000h (40.5 KiB = 0x0A20 paragraphs at top of RAM)  │
│  - 0x0000..0x7FFF : Transient code & rodata (protected by CRC-16-CCITT)  │
│  - 0x8000..0x80FF : Scratch state, DBCS bitmap, DOSKEY 128B recall buf   │
│  - 0x8100..0x849F : Cmdline (256B), Batch %1..%9 (256B), FOR stack (416B)│
│  - 0x84A0..0x8DA0 : DOS 1.25 fallback env (512B) + I/O work buf (1,792B) │
│  - 0x8DA0..0x9FFE : Per-statement AST & string bump arena (4.6 KiB)      │
│  - 0x9FFE..0xA1FE : Canary + Transient stack (TRN_STACK_TOP = 0xA1FE)    │
└──────────────────────────────────────────────────────────────────────────┘
(Section 3: INIT executes once at startup, relocates TRANSIENT with overlap-safe
 STD/CLD REP MOVSW, shrinks the resident MCB, runs AUTOEXEC.BAT under /P, and
 is discarded completely.)
```

**4-Era Kernel HAL Dispatch (`asm/dos16/hal/`)**:
- **MS-DOS `1.25` (`k_dos125.inc`, `res/ldr_125.inc`, `res/red_125.inc`)**: Detected when `INT 21h AH=30h` returns `AL=0`. Uses 37-byte FCBs with `wRecSize = 1` for byte-accurate batch/file reads, `INT 25h` raw boot/FAT12 sector reads for `DIR` free-space calculation, `red_125` resident `INT 21h` (`AH=01h..0Ah`) hooks to bring `<`, `>`, `>>`, and `|` to DOS 1.25, and `ldr_125` to build child PSPs and load external `.COM` binaries without `AH=4Bh`. (`WINCMD1.COM` provides a stripped `1,088`-byte DOS 1.25-dedicated variant, and `WINCMDMZ.EXE` provides a relocatable `MZ` executable target.)
- **MS-DOS `2.00` (`k_dos200.inc`, `res/exec_200.inc`)**: Introduces file handles (`AH=3Ch..45h`), hierarchical directories, `DUP`/`DUP2` redirection (`AH=45h`/`46h`), and `exec_200` — saving `SS:SP` inside `cs:` resident memory before `INT 21h AX=4B00h` because MS-DOS 2.x destroys all CPU registers including `SS:SP` upon child return.
- **MS-DOS `4.00` (`k_dos400.inc`, `cp_dbcs.inc`, `res/ems_40.inc`, `wincmd_m.asm`)**: Adds `AX=6C00h` extended open, `AX=6900h` volume serial numbers, `AH=59h` extended errors backed by resident `/MSG` (`WINCMD.MSG`), `AX=6300h`/`6507h` DBCS lead-byte table awareness (never splitting 2-byte characters on `0x5C` backslash, `|`, `&`, or `Backspace`), and LIM `EMS 4.0` (`INT 67h`) zero-disk-seek transient restoration when `CRC-16` (`core/crc16.inc`) detects transient overwrite.
- **MS-DOS `7.x / Win9x` (`k_dos700.inc`)**: Negotiates `INT 21h AX=71xxh` Long Filename (`LFN`) VFS calls (remembering to close LFN find handles via `AX=71A1h`!) and `AX=7303h` (`Get_ExtFreeSpace`) 64-bit `FAT32` disk sizes (`core/math64.inc`).

### 7.2 `win16`: Real & 80286 Protected-Mode `NE` Architecture

`asm/win16/` produces three artifacts for 16-bit Windows (`1.01` through `3.2`):
- **`WINCMD16.EXE` (`w16_main.asm`, `11,264 B`)**: Universal `8086`-clean `NE` executable running in Windows `1.0x`/`2.0x` Real Mode, `286` Standard Mode, and `386` Enhanced Mode.
- **`WINCMD286.EXE` (`w16_286.asm`, `11,776 B`)**: `.286p` Protected-Mode `NE` adding a third `MOVEABLE | DISCARDABLE` segment (`_TRN286`) invoked via `INTERNALREF` far fixups so Windows's Protected-Mode memory manager can discard and reload transient helpers under memory pressure.
- **`WINCMD.PIF` (`w16_pif.asm`, `545 B`)**: Authentic `369`-byte classic PIF header plus `WINDOWS 286 3.0` and `WINDOWS 386 3.0` windowed/background/idle-detection extension blocks for `WINOLDAP`.

**Key Subsystems**:
- **GDI Terminal (`w16_term.inc`)**: Custom window class rendering an `80x25`–`132x50` fixed-pitch character grid via `BeginPaint`/`TextOut`/`ScrollWindow`, blinking caret (`SetCaretPos`), clipboard copy/paste (`CF_TEXT`), and non-blocking `PeekMessage` + `INT 2Fh AX=1680h` idle slice yielding.
- **Dynamic Win16 Capability Resolver (`w16_hal.inc`)**: Static `NE` ordinal imports use only Windows `1.01` `KERNEL`/`USER`/`GDI` baselines (`InitTask`, `WaitEvent`, `InitApp`, `TextOut`), dynamically resolving `WinExec`/`GetFreeSpace` (`Win 3.0+`), `TOOLHELP.DLL!MemManInfo` (`Win 3.1+`), `WNetAddConnection`/`WNetCancelConnection` (`WfW 3.11` UNC `PUSHD`/`POPD`), and `IsDBCSLeadByte` (`Win 3.2`).
- **Windows 3.2 `GB2312` / `CP936` DBCS & IME (`w16_dbcs.inc`)**: Double-cell character wrapping before lead bytes, paired `Backspace`/`Left`/`Right` cursor motion, and `WM_IME_REPORT` (`IR_STRING`) composition handling.
- **Hybrid Child Dispatcher (`w16_exec.inc`)**: Runs internal commands and `.BAT` scripts directly in-process inside the GDI window; inspects external binaries (`MZ` vs `NE` at `[MZ+0x3C]`) to launch Win16 `NE` apps via `WinExec` + `GetModuleUsage` polling or 16-bit DOS apps via `WINOLDAP` / `WINCMD.PIF`.

### 7.3 `x86_32`: CRT-Free `IA-32` `PE32` (`Subsystem 3.10`) Architecture

`WINCMD32.EXE` (`asm/x86_32/wincmd32.asm`, `56,832 B`) spans **every 32-bit Windows release from NT `3.1` (`1993`) and Windows `95` (`1995`) through Windows `11` WOW64** inside a single `i386` binary:
- **Static Import Minimalism**: Imports only baseline NT `3.1` / Win95 `KERNEL32.DLL` functions (`asm/tc/arm_ms/k32_x86.def`) so the Windows `NT 3.1` and `Windows 95 RTM` PE loaders never fail on missing symbols.
- **Runtime OS & Capability Dispatch (`hal_dyn.inc`)**: Queries `GetVersion()` at boot to bind the active HAL vtable to either **Unicode NT (`hal_ntw.inc`, `*W` APIs + extended-length `32K` long paths)** or **ANSI/DBCS Win9x (`hal_9xa.inc`, `*A` APIs + `IsDBCSLeadByte`)**, then progressively probes `GetFileAttributesExW` (`NT 4.0+`), `CreateHardLinkW` / `DeviceIoControl` junctions (`Win2000+`), `CreateSymbolicLinkW` (`Vista+`, plus `ALLOW_UNPRIVILEGED_CREATE` on `Win10 1703+`), `SetConsoleMode` VT (`Win10+`), `ADVAPI32.DLL` Registry APIs (`ASSOC`/`FTYPE`), `MPR.DLL` (`PUSHD` UNC network share mapping), and `SHELL32.DLL` (`START`).
- **Diagnostic Compatibility Caps**: Supports `--win9x-ansi-sim` (forces the Win9x `*A` HAL on modern NT/Wine for CI testing) and `--nt31-api-cap` (masks all post-NT-3.1 dynamic APIs to test NT 3.1 fallback code paths on modern Windows).

### 7.4 `x86_64`: CRT-Free `AMD64` `PE32+` (`Subsystem 5.02`) Architecture

`WINCMD64.EXE` (`asm/x86_64/wincmd64.asm`, `69,632 B`) targets 64-bit Windows from `XP Professional x64` / `Server 2003 SP1` (`5.02`) through `Windows 11`:
- **Full `.pdata` / `.xdata` Unwind Tables**: Every non-leaf function uses `@proc_frame`, emitting deterministic `RUNTIME_FUNCTION` + `UNWIND_INFO` entries so Structured Exception Handling (`SEH`), `Ctrl+C` unwinding, ETW stack walking, and debuggers unwind cleanly across every frame.
- **Modern Exploit Mitigations**: Linked with `/HIGHENTROPYVA` (clean 64-bit pointers above `4 GiB`), `/DYNAMICBASE`, `/NXCOMPAT`, `/GUARD:CF` (`Control Flow Guard`), Intel `CET` shadow-stack compatibility (strictly balanced `CALL`/`RET`), and an embedded `longPathAware` application manifest (`wincmd64.manifest`).
- **Capability Simulation**: Supports `--nt52-api-cap` to cap runtime API resolution at Windows Server 2003 / XP x64 (`5.2`) during regression testing.

### 7.5 `arm32`: `ARMv4` WinCE (`0x01C0`) & `ARMv7-A` Thumb-2 `ARMNT` (`0x01C4`)

`asm/arm32/` shares a unified 32-bit ARM shell core (`core/str_utf.inc`, `read/stream_ed.inc`, `parse/exp_prs.inc`, `vfs/path_glob.inc`, `rt/env_sa.inc`, `rt/exec_ast.inc`, `rt/exec_for.inc`, `cmd/cmds_{a,b}_a32.inc`, `vm8086/`) anchored on an `8`-byte-aligned `2,048`-byte context block (`g_a32_ctx`, keeping all fields at `< 2,048` byte offsets for single-instruction `LDR`/`STR` addressing!) over two distinct OS HALs:
- **`v4/wincmdv4.asm` + `v4/hal_ce4.inc` (`WINCMD_ARMV4.EXE`, `65,024 B`)**: Targets **Windows CE `3.0–6.0` / Pocket PC / Embedded Compact** (`Machine = 0x01C0`, `Subsystem = 9` `WINDOWS_CE_GUI`). Links exclusively against `COREDLL.DLL` Unicode APIs, synthesizes per-process CWD and environment blocks (since WinCE has no kernel current-directory concept and uses rootless `\Windows` / `\Storage Card` object-store paths), uses software shift-subtract `udiv32`/`sdiv32`, and performs byte-wise unaligned accesses so `ARMv4`/`ARMv5` CPUs never rotate or fault on unaligned pointers.
- **`v7/wincmdv7.asm` + `v7/hal_rt7.inc` (`WINCMD_ARM32.EXE`, `50,688 B`)**: Targets **Windows RT `8.0/8.1` and Windows 10 `ARM32`** (`Machine = 0x01C4` `ARMNT`, `Subsystem = 6.02`). Emits pure Thumb-2 `UAL` with strict `IT` blocks (`<= 4` instructions, single condition, no calls/branches except last slot), `MRC p15, 0, Rd, c13, c0, 2` `TEB` reads, `KERNEL32.DLL` imports, `GUARD_CF`, and `.pdata` unwind metadata.

### 7.6 `arm64`: `AArch64` `PE32+` (`0xAA64`, `Subsystem 10.0`) Architecture

`WINCMD_ARM64.EXE` (`asm/arm64/wincmda64.asm`, `69,120 B`) targets native **Windows 10 (`1709+`) and Windows 11 on ARM**:
- **Windows ARM64 ABI & `X18` Invariance**: `X18` (user-mode `TEB` pointer) is never written anywhere in the shell, `vm8086`, or any of the `19` ARM64 utilities (statically enforced by `asm-lint`).
- **Hardware `SP` Alignment & Stack Probes**: All stack frames allocate `16`-byte multiples (`stp x29, x30, [sp, #-N]!`), never access memory through an unaligned `SP`, and invoke `__chkstk` (`X15 = bytes / 16`) when frame size `>= 4 KiB`.
- **ARMv8.3+ `PAC` & `ARMv8.5+` `BTI`**: Prologues and epilogues emit `HINT #25` (`PACIASP`) and `HINT #29` (`AUTIASP`) (plus `BTI c` on indirect targets), executing as architectural `NOP`s on `ARMv8.0-A` (Snapdragon 835/850) while providing hardware return-address signing on `ARMv8.3+` (Snapdragon 8cx Gen 3 / X Elite / Apple Silicon Windows 11 VMs).

---

## 8. Shell Language, Reader, 5-Phase Expander & AST Execution Engine

### 8.1 Stateless Byte-Offset Batch & Interactive Reader
- **Batch Reader (`read/stream*.inc`)**: Each active `.BAT`/`.CMD` invocation (up to `32` nested `CALL` frames on PE targets; resident stack on DOS) stores only `{ szBatchPath, u64ByteOffset, savedEcho, argVector }`. Fetching the next logical line opens the script with `FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE`, seeks to `u64ByteOffset`, reads up to the next `CRLF`/`LF` or `0x1A` (`Ctrl+Z` EOF), joins `^`-escaped line continuations and multi-line `( ... )` parenthesized blocks, updates `u64ByteOffset`, and **closes the file handle immediately** before AST evaluation.
- **Interactive Line Editor (`read/line_ed.inc`, `con32.inc`, `con64.inc`, `stream_ed*.inc`)**: Supports `Left`/`Right`/`Home`/`End`, `Insert`/`Delete`/`Backspace`/`Esc`, `Up`/`Down`/`F7`/`F8`/`F9` `64`-entry command history, classic `F1`–`F5` template keys, `DOSKEY` macro expansion (`$1`–`$9`, `$*`, `$T`, `$G`, `$L`, `$B`), and `Tab` / `Shift+Tab` filesystem completion (`/F:ON`).

### 8.2 5-Phase Variable, Parameter & Modifier Expansion
To match `CMD.EXE` and `COMMAND.COM` quirk-for-quirk, expansion is strictly partitioned into five ordered phases:
1. **Phase 0 — Batch Parameter & Modifier Substitution**: Expands `%0`–`%9`, `%*`, and `%~` modifiers (`f` full path, `d` drive, `p` path, `n` name, `x` ext, `s` short 8.3, `a` attributes `--a------`, `t` timestamp, `z` byte size, `$VAR:` directory search, and combinable chains such as `%~dp0` or `%~nx1`). Inside batch files, `%%` unescapes to a literal `%`.
2. **Phase 1 — Parse-Time `%VAR%` Expansion**: Expands `%VAR%`, `:old=new` case-insensitive replacement (`%*` prefix replacement included), `:~start[,len]` positive/negative substring slicing, and dynamic pseudo-variables (`%CD%`, `%DATE%`, `%TIME%`, `%RANDOM%`, `%ERRORLEVEL%`, `%CMDEXTVERSION%`, `%CMDCMDLINE%`, `%HIGHESTNUMANODENUMBER%`) **before** compound `( ... )` AST parsing.
3. **Phase 2 — Tokenization & Compound AST Construction**: Splits operators (`&`, `&&`, `||`, `|`, `(`, `)`, `<`, `>`, `>>`, `2>&1`) outside `"..."` quotes and `^` escapes into an arena-allocated AST.
4. **Phase 3 — `FOR` Loop Variable Substitution**: Substitutes active `%%I` (batch) / `%I` (interactive) `FOR` variables (with full `%~dpnxfsatz$PATH:I` modifier support) on each iteration without re-tokenizing structural operators.
5. **Phase 4 — Execution-Time Delayed Expansion (`/V:ON`) & Escape Stripping**: When `EnableDelayedExpansion` is active and `!` appears on the command, expands `!VAR!`, `!VAR:old=new!`, and `!VAR:~s,l!` immediately before executing each simple command, then strips unquoted `^` escapes.

### 8.3 Bump-Arena Lexer & Compact AST (`ast_kind.inc`)
Every logical statement allocates its tokens, strings, redirection descriptors, and AST nodes from a fast per-statement bump arena reset in `O(1)` once the top-level statement finishes:
- **Node Discriminants (`ast_kind.inc`)**:
  - `AST_NOP (0)` — blank line, `REM`, or `:label`
  - `AST_CMD (1)` — simple internal/external command + `REDIR_NODE` list
  - `AST_PIPE (2)` — `lhs | rhs` pipeline
  - `AST_SEQ (3)` — `lhs & rhs` unconditional sequence
  - `AST_AND (4)` — `lhs && rhs` (`ERRORLEVEL == 0` short-circuit)
  - `AST_OR (5)` — `lhs || rhs` (`ERRORLEVEL != 0` short-circuit)
  - `AST_SUBSHELL (6)` — `( list )` parenthesized block (supports block-level redirection!)
  - `AST_IF (7)` — `IF [NOT] [/I] cond (then_ast) [ELSE (else_ast)]` (`IF_ERRORLEVEL`, `IF_EXIST`, `IF_DEFINED`, `IF_CMDEXTVER`, `IF_STREQ` `==`, `IF_EQU`/`NEQ`/`LSS`/`LEQ`/`GTR`/`GEQ`)
  - `AST_FOR (8)` — `FOR` (`FOR_PLAIN`, `FOR_DIR` `/D`, `FOR_REC` `/R`, `FOR_NUM` `/L`, `FOR_PARSE` `/F`)

### 8.4 11-Tier Pratt `SET /A` Arithmetic Engine
`rt/arith*.inc` and `rt/env_sa*.inc` implement a recursive/Pratt precedence-climbing evaluator over signed 32-bit two's-complement integers across all 11 `CMD.EXE` precedence levels:
1. `( expr )`, unary `+`, `-`, `!` (logical NOT), `~` (bitwise NOT)
2. `*`, `/`, `%` (guarded against `/ 0` -> `ERR_DIV_ZERO` and `0x80000000 / -1` -> `-2147483648` without triggering hardware `#DE` `INT 00h`!)
3. `+`, `-`
4. `<<`, `>>` (masked `& 31`)
5. `&`
6. `^`
7. `|`
8. Assignment `=`, `*=`, `/=`, `%=`, `+=`, `-=`, `&=`, `^=`, `|=`, `<<=`, `>>=` (writing results back into the active environment scope)
9. `,` comma sequence operator

Supports decimal, `0x` hexadecimal, `0` octal literals, and bare variable names (implicitly evaluating undefined variables to `0`).

### 8.5 Redirection, Concurrent `CreatePipe` & Spooled Pipelines
- **Redirection (`<`, `>`, `>>`, `n>&m`, `n<&m`)**: Duplicates original OS handles (`INT 21h AH=45h` on DOS; `DuplicateHandle` on Win32/64/ARM), binds target files/devices (`NUL`, `CON`, `CONIN$`, `CONOUT$`, `PRN`, `AUX`, `COM1`–`COM9`, `LPT1`–`LPT9`), executes the node (internal built-in, subshell block, `FOR`/`IF`, external process, or `vm8086` `.COM`), and deterministically restores original handles afterward.
- **Concurrent vs. Spooled Pipelines**:
  - On **Win32 / Win64 / ARMv7 / ARM64 (`pipe32.inc`, `pipe64.inc`, `hal_rt7.inc`, `hal_a64.inc`)**: Creates anonymous OS pipes (`CreatePipe`), spawns `"<self.exe>" /S /D /C "<stage>"` child processes concurrently with inheritable `hStdInput`/`hStdOutput`, closes parent pipe ends immediately so EOF propagates cleanly, waits on all stage handles, and returns the rightmost stage's exit code.
  - On **Single-Tasking MS-DOS (`pipe_spl.inc`) & Pipe-Less WinCE (`hal_ce4.inc`)**: Generates collision-free `%TEMP%` spool files, runs stages sequentially with redirected handles, and unlinks spool files on completion or `Ctrl+C`.

### 8.6 Built-In Command Dispatcher (`cmd_ids.inc`)
Command names are matched case-insensitively against a sorted 47-entry dispatch table (`0x01` `CMD_ASSOC` through `0x2F` `CMD_VOL`), handling classic no-space syntax quirks (`ECHO.`, `ECHO/`, `ECHO:`, `CD..`, `CD\root`, `MD\foo`, `RD\foo`) before falling through to `PATHEXT` (`.COM;.EXE;.BAT;.CMD`) external search. Notable architectural details among built-ins:
- **`RD /S [/Q]` Reparse-Point Safety**: Checks `FILE_ATTRIBUTE_REPARSE_POINT` (`0x0400`) during directory traversal and calls `RemoveDirectoryW` directly on `<JUNCTION>` and `<SYMLINKD>` nodes **without recursing into or deleting target files**.
- **`MKLINK` (`/D`, `/H`, `/J`)**: Uses `CreateSymbolicLinkW` (with `SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE` fallback for Windows 10/11 Developer Mode), `CreateHardLinkW` for `/H`, and raw `DeviceIoControl(FSCTL_SET_REPARSE_POINT)` with synthesized `REPARSE_DATA_BUFFER` (`IO_REPARSE_TAG_MOUNT_POINT = 0xA0000003`) for `/J` junctions.
- **`SETLOCAL` / `ENDLOCAL`**: Maintains a 32-deep stack of environment snapshots, current working directory, active drive, `EnableExtensions`, and `EnableDelayedExpansion` flags.

---

## 9. Embedded Multi-ISA `vm8086` 16-Bit `.COM` & `MZ` Virtual Executor

64-bit Windows (`x86-64` and `ARM64`) and `ARM32` Windows (`WinCE` / `WinRT`) completely removed hardware V86 mode (`NTVDM`), normally preventing `.BAT`/`.CMD` scripts from invoking classic 16-bit `.COM` and DOS `MZ` `.EXE` tools. `win-cmd-asm` solves this natively inside `WINCMD32.EXE`, `WINCMD64.EXE`, `WINCMD_ARMV4.EXE`, `WINCMD_ARM32.EXE`, and `WINCMD_ARM64.EXE` (`asm/*/vm8086/`):

1. **Binary Header Inspection**: Whenever external process creation encounters `ERROR_BAD_EXE_FORMAT` (`193`) / `ERROR_EXE_MACHINE_TYPE_MISMATCH` (`216`), `.COM` extension, pure `MZ` header without a `PE\0\0` or `NE` secondary header, or `--vm8086`, control transfers in-process to `vm8086`.
2. **Isolated `1 MiB + 64 KiB` Guest Address Space (`VM86_CTX`)**:
   - Allocates `1,114,112` bytes (`0x110000` for 20-bit `seg << 4 + off` + High Memory Area wrap safety) via `VirtualAlloc`.
   - Synthesizes a DOS environment segment (`0x0060`), a 256-byte `PSP` (`0x1000` for `.COM` or `0x1000` + `0x1010` image base for `MZ`), `INT 20h` (`CDh 20h`) at `PSP:0000h`, formatted default `FCB`s at `005Ch`/`006Ch`, `80h` command-tail + default `DTA`, and applies `MZ` relocation fixups (`CS`, `SS:SP`, `DS=ES=PSP`).
3. **Pure-Assembly 8086 CPU Core**:
   - Decodes segment overrides (`26h`/`2Eh`/`36h`/`3Eh`), `REP`/`REPNE` (`F2h`/`F3h`), full 8086 ModR/M addressing modes, 8/16-bit ALU & lazy/exact `CF`/`PF`/`AF`/`ZF`/`SF`/`OF`/`DF` flag synthesis, `MUL`/`IMUL`/`DIV`/`IDIV`, shifts/rotates, string primitives (`MOVS`, `CMPS`, `SCAS`, `LODS`, `STOS`), `LOOP`/`JCXZ`, `CALL`/`RET`/`RETF`/`INT`/`IRET`, and `HLT`.
   - Host `RSP`/`SP` and `X18` (`TEB`) remain 100% isolated from guest `SS:SP`.
4. **Transparent DOS-to-Win32 Syscall Bridge**:
   - Translates `INT 20h` and `INT 21h` (`AH=01h..0Ah` console I/O, `0Eh`/`19h` drives, `1Ah`/`2Fh` DTA, `25h`/`35h` guest IVT vectors, `2Ah`–`2Dh` system clock, `30h` DOS version, `39h`–`3Bh` mkdir/rmdir/chdir, `3Ch`–`46h` file open/create/read/write/lseek/unlink/chmod/dup mapped onto a 20-slot host `HANDLE` table inheriting active shell pipes/redirections!, `47h` getcwd, `48h`–`4Ah` guest memory blocks, `4Ch` exit with `AL` -> shell `%ERRORLEVEL%`, `4Eh`/`4Fh` `FindFirst`/`FindNext`, `56h` rename), plus `INT 10h` TTY, `INT 16h` keyboard, `INT 29h` fast console out, and `INT 67h` EMS stub.

---

## 10. Standalone 19-Utility Companion Suite Architecture

All `19` companion utilities are implemented natively five times (`asm/dos16/util/*.asm`, `asm/x86_32/util/*.asm`, `asm/x86_64/util/*.asm`, `asm/arm32/util/*.asm`, `asm/arm64/util/*.asm` — **95 standalone CRT-free binaries**):

- **Shared Per-ISA Micro-Runtime (`u_common*.inc`, `u_imps*.inc`, `u_data*.inc` / `u_rdata*.inc` / `u_bss*.inc`)**: Provides `VirtualAlloc` bump-arena setup, command-tail tokenizer, WTF-8 <-> UTF-16LE extended-length (`32K`) path converters, buffered console/file writers, and 64-bit unsigned decimal/hex formatters in `< 2 KiB` of shared code.
- **Algorithmic Highlights**:
  - **`FINDSTR`**: Compiles regex patterns (`^`, `$`, `.`, `*`, `[abc]`, `[^a-z]`, `\<`, `\>`) into a **64-state bit-parallel Thompson NFA** stepping in `O(1)` register operations per input byte with zero backtracking blowup.
  - **`SORT`**: Implements an arena-backed **`O(N log N)` bottom-up merge sort** supporting `/R` reverse, `/+n` column offset, `/UNIQUE` deduplication, and `/O <out>`.
  - **`FC` & `COMP`**: Line-oriented resynchronizing diff (`FC /N /C /W /LBn`) and binary/hex/decimal fixed-offset comparator (`COMP /D /A /L /N=num /C`).
  - **`ROBOCOPY`**: On 32/64-bit Windows (`x86_32`, `x86_64`, `arm32`, `arm64`), implements a **real multi-threaded `/MT[:n]` (`1..128`, default `8`) worker pool** via `CreateThread` + `WaitForMultipleObjects`, partitioning queued `(src, dst)` copy slots across lock-free thread strides with thread-local UTF-16LE stack buffers (`GUARD_CF`/`CET`-clean) and authentic bitmask exit codes (`0..16`).
  - **`REG`**: Dynamically resolves `ADVAPI32.DLL` Unicode Registry APIs (`RegOpenKeyExW`, `RegCreateKeyExW`, `RegQueryValueExW`, `RegEnumKeyExW`, `RegEnumValueW`, `RegSetValueExW`, `RegDeleteKeyW`, `RegDeleteValueW`) supporting `QUERY`, `ADD`, `DELETE`, `COPY`, `EXPORT`, `IMPORT`, `SAVE`, `RESTORE`, `COMPARE`, and `FLAGS` (and an `.INI`-backed Registry store on DOS16).
  - **`WHOAMI`**: Queries `OpenProcessToken` + `GetTokenInformation` (`TokenUser`, `TokenGroups`, `TokenPrivileges`) + `LookupAccountSidW` and formats binary `SID` structures (`S-1-5-21-...`) directly in pure assembly without `ConvertSidToStringSidW` or CRT `sprintf`.

---

## 11. Deterministic 5-ISA Emulation, Differential Fuzzing & Media Mastering

### 11.1 `tools/emu-cov`: Deterministic 5-ISA User-Mode & System Emulators
`emu-cov` executes compiled `win-cmd-asm` binaries deterministically on any macOS, Linux, or Windows host without requiring privileged hypervisors:
- **`DosVm` + `dos_syscalls.rs` (`8086` / `80286` Real Mode)**: Simulates `1 MiB + 64 KiB` RAM, MCB chains, PSP/FCB/DTA structures, `EMS 4.0`, and era-exact quirks across `DosEra::{Dos125, Dos200, Dos400, Dos710}` (including `DOS 2.00` `FILES=8` handle caps and `AH=4Bh` `SS:SP` clobbering!).
- **`Win16State` (`win16_ne.rs`)**: Loads multi-segment `NE` binaries into descriptor-checked Protected-Mode selectors (raising immediate `#GP` faults on illegal segment arithmetic!), dispatches ordinal thunks (`KERNEL`, `USER`, `GDI`, `KEYBOARD`, `WNet`, `TOOLHELP`), and captures GDI `TextOut` coordinates/strings.
- **`X86PeVm` (`x86_pe.rs`)**: Executes both 32-bit `IA-32` `PE32` and 64-bit `x86-64` `PE32+` images (enforcing Win64 `16`-byte `RSP` alignment at IAT boundaries and cooperatively scheduling `CreateThread` worker threads for `ROBOCOPY /MT`!).
- **`Arm32Vm` (`arm32_pe.rs`) & `Arm64Vm` (`arm64_pe.rs`)**: Execute `ARMv4` (`0x01C0` `COREDLL`), `ARMv7-A` Thumb-2 (`0x01C4` `ARMNT` including `IT` predication), and `AArch64` (`0xAA64` enforcing `X18` immutability and `16`-byte `SP` alignment) against the shared `SharedWinEnv` (`win_mock.rs`) virtual filesystem, Registry, token, and recursive child-process/pipe harness.
- **LCOV Coverage Tracing**: Every VM records executed instruction PCs (`DA:`) and conditional branch taken/not-taken edges (`BRDA:`), serializing standard `lcov.info` trace files.

### 11.2 `tools/diff-fuzz`: SplitMix64 Cross-Assembler, 7-ISA & Live `cmd.exe` Fuzzer
Generates thousands of deterministic batch scripts + synthetic 16-bit `CHILD.COM` binaries from a 64-bit `SplitMix64` PRNG seed exercising `SET /A` arithmetic, `SETLOCAL`/`ENDLOCAL`, `IF`/`ELSE` comparisons, `FOR` loops, `CALL :label` subroutines, `COPY`/`REN`/`DEL` VFS mutations, `<`/`>` redirection, and child `ERRORLEVEL` propagation:
- **DOS Cross-Assembler Mode**: Runs identical cases across `NASM`, `FASM`, and `JWasm` `WINCMD.COM` builds across eras, asserting identical `stdout`, `stderr`, `exit_code`, and post-run VFS state.
- **7-Target Cross-ISA + Reference `cmd.exe` Mode (`--cross-isa --ref-cmd N`)**: Cycles every generated script across `WINCMD.COM` (`8086`), `WINCMD16.EXE`/`WINCMD286.EXE` (`Win16 NE`), `WINCMD32.EXE` (`IA-32`), `WINCMD64.EXE` (`x86-64`), `WINCMD_ARMV4.EXE` (`ARMv4`), `WINCMD_ARM32.EXE` (`ARMv7`), and `WINCMD_ARM64.EXE` (`ARM64`) — rotating across all assembler backends — and cross-checks against live `cmd.exe` (`wine cmd.exe` on macOS/Linux or native `cmd.exe` on Windows).

### 11.3 `tools/media-pkg`: Bit-Reproducible Release Media Masterer
Constructs **13 deterministic release artifacts** from scratch in pure zero-`unsafe` Rust with normalized timestamps (`1980-01-01 00:00:00`), fixed volume serials, and sorted directory entries:
- **`fat12.rs`**: Formats `160 KiB` (40×1×8), `360 KiB` (40×2×9), `720 KiB` (80×2×9), and `1.44 MiB` (80×2×18) `FAT12` floppy images (`BPB` + dual `FAT12` tables + root directory + data clusters).
- **`iso9660.rs`**: Masters a Level-1 `ISO-9660` (`ECMA-119` Primary Volume Descriptor + Path Tables + hierarchical `DOS16/`, `WIN16/`, `WIN32/`, `WIN64/`, `ARM32/`, `ARM64/` directories) universal optical disc (`wincmd-win-universal.iso`).
- **`archive.rs`**: Emits deterministic `.ZIP` archives, POSIX `USTAR` `.tar.gz` standalone pre-lowered source distributions (`wincmd-src-{nasm,fasm,jwasm}.tar.gz`, each bundled with a zero-Rust `build-standalone.sh` / `BUILD.BAT`), and `SHA256SUMS.txt` (implemented with an internal pure-Rust FIPS 180-4 `SHA-256` + `CRC-32` engine requiring zero external crypto crates).
