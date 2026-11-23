# `win-cmd-asm` Usage, Scripting & Operations Manual

This manual covers building, verifying, packaging, booting, and operating every shell executable, batch-language construct, internal built-in command, companion utility, embedded `vm8086` 16-bit runner, and Rust CLI verification tool in **`win-cmd-asm`**.

---

## Table of Contents

1. [Host Prerequisites & Toolchain Bootstrap](#1-host-prerequisites--toolchain-bootstrap)
2. [Building, Verifying & Packaging From Source](#2-building-verifying--packaging-from-source)
   - [2.1 POSIX Hosts (`macOS`, `Linux`, `MSYS2`) via GNU `Makefile`](#21-posix-hosts-macos-linux-msys2-via-gnu-makefile)
   - [2.2 Native Windows (`NT 3.1` – `Windows 11`) via `make.bat` / `BUILD.CMD`](#22-native-windows-nt-31--windows-11-via-makebat--buildcmd)
   - [2.3 Native MS-DOS (`2.00` / `4.00` / `7.x`) Self-Hosting via `BUILD.BAT` & `ASMCONV.COM`](#23-native-ms-dos-200--400--7x-self-hosting-via-buildbat--asmconvcom)
   - [2.4 Zero-Rust Standalone Release Tarballs](#24-zero-rust-standalone-release-tarballs)
3. [Shell Executables & Startup Switches Reference](#3-shell-executables--startup-switches-reference)
   - [3.1 MS-DOS (`WINCMD.COM`, `WINCMD1.COM`, `WINCMDMZ.EXE`)](#31-ms-dos-wincmdcom-wincmd1com-wincmdmzexe)
   - [3.2 16-Bit Windows (`WINCMD16.EXE`, `WINCMD286.EXE`, `WINCMD.PIF`)](#32-16-bit-windows-wincmd16exe-wincmd286exe-wincmdpif)
   - [3.3 32/64-Bit Windows & Windows CE (`WINCMD32`, `WINCMD64`, `WINCMD_ARMV4`, `WINCMD_ARM32`, `WINCMD_ARM64`)](#33-3264-bit-windows--windows-ce-wincmd32-wincmd64-wincmd_armv4-wincmd_arm32-wincmd_arm64)
4. [Interactive Line Editing, `DOSKEY` & `PROMPT` Reference](#4-interactive-line-editing-doskey--prompt-reference)
5. [Batch Scripting Language, Expansion & Operators Guide](#5-batch-scripting-language-expansion--operators-guide)
   - [5.1 Pipelines, Redirection & Conditional Chaining](#51-pipelines-redirection--conditional-chaining)
   - [5.2 Variables, Substrings, Replacements & Delayed Expansion (`!VAR!`)](#52-variables-substrings-replacements--delayed-expansion-var)
   - [5.3 Batch Parameters & `%~` Path/Metadata Modifiers](#53-batch-parameters---pathmetadata-modifiers)
   - [5.4 `SET /A` 11-Tier Signed 32-Bit Arithmetic](#54-set-a-11-tier-signed-32-bit-arithmetic)
   - [5.5 `IF`, `FOR` (`/D`, `/R`, `/L`, `/F`), `CALL :label` & `SETLOCAL`](#55-if-for-d-r-l-f-call-label--setlocal)
6. [Complete 47 Internal Built-In Commands Reference](#6-complete-47-internal-built-in-commands-reference)
7. [Running 16-Bit DOS `.COM` & `MZ` Programs on 64-Bit & ARM Windows (`vm8086`)](#7-running-16-bit-dos-com--mz-programs-on-64-bit--arm-windows-vm8086)
8. [Complete 19 Companion Utilities Reference (`*.COM` & `*.EXE`)](#8-complete-19-companion-utilities-reference-com--exe)
9. [Rust CLI Verification, Emulation, Fuzzing & Packaging Tools](#9-rust-cli-verification-emulation-fuzzing--packaging-tools)
10. [Booting Mastered Media (`.IMG` / `.ISO`) & Replicating CI](#10-booting-mastered-media-img--iso--replicating-ci)

---

## 1. Host Prerequisites & Toolchain Bootstrap

### Required & Auto-Bootstrapped Tools on POSIX (`macOS` / `Linux` / `MSYS2`)
- **Rust (`1.85+`, Edition 2024)**: Required for `asm-conv`, `asm-lint`, `emu-cov`, `diff-fuzz`, and `media-pkg`.
- **LLVM (`llvm-mc`, `lld-link`, `llvm-dlltool`)**: Links `PE32`/`PE32+` COFF objects deterministically (`/BREPRO`, `/NODEFAULTLIB`) and assembles ARMv4/ARMv7/ARM64 LLVM targets.
  - macOS: `brew install llvm nasm dos2unix`
  - Ubuntu/Debian: `apt-get install -y nasm llvm lld clang cmake ninja-build dos2unix`
- **Multi-Assembler Bootstrap (`scripts/bootstrap-toolchains.sh`)**:
  Automatically invoked by `Makefile` / `scripts/build-all.sh`. Any assembler not already in `PATH` (`jwasm`, `fasm`, `fasmarm`) is automatically downloaded/built into `${WINCMD_TC_DIR:-/tmp/wincmd-toolchains}/bin` (generating transparent `wine`/`wine64` wrappers on macOS or non-x86 hosts when needed).

---

## 2. Building, Verifying & Packaging From Source

### 2.1 POSIX Hosts (`macOS`, `Linux`, `MSYS2`) via GNU `Makefile`

```bash
# Run full pipeline: lint -> lower -> build (all toolchains & ISAs) -> verify -> test
make all

# Targeted builds by toolchain or CPU architecture
make build TOOLCHAIN=nasm TARGET=dos16
make build TOOLCHAIN=nasm,fasm,jwasm TARGET=x86_64
make build TOOLCHAIN=fasmarm,llvm TARGET=arm64

# Individual quality, fuzzing & release targets
make lint          # 100% @tag doc, FAT 8.3, CFG stack, @clobbers & ABI checks (239 modules)
make lower         # Lower asm/ -> dist/src/{nasm,fasm,fasm-pe32,fasm-coff,jwasm,fasmarm,llvm-mc,armasm}
make verify        # Cross-assembler equivalence, ISA opcode purity & footprint budgets
make test          # Cargo unit + multi-ISA integration tests + 5-ISA emu-cov runs
make fuzz          # 2,500 DOS cross-assembler + 10,000 7-ISA + 25 live cmd.exe fuzz cases
make package       # Master 13 reproducible .img, .iso, .zip & .tar.gz artifacts in dist/release/
make coverage      # Rust workspace coverage report (requires cargo-llvm-cov)
make hooks         # Configure .githooks/pre-commit (6-stage lint/build/test/EOL/badge hook)
make clean         # Remove build/, dist/, and lcov.info
```

Build outputs are organized under `build/<toolchain>/<target>/`:
- `build/{nasm,fasm,jwasm}/dos16/` — `WINCMD.COM`, `WINCMD1.COM`, `WINCMDMZ.EXE`, `ASMCONV.COM`, `WINCMD.MSG`, and `19` `*.COM` utilities
- `build/{nasm,fasm,jwasm}/win16/` — `WINCMD16.EXE`, `WINCMD286.EXE`, `WINCMD.PIF`
- `build/{nasm,fasm,fasm-pe32,jwasm}/x86_32/` — `WINCMD32.EXE` and `19` `*.EXE` utilities
- `build/{nasm,fasm,fasm-coff,jwasm}/x86_64/` — `WINCMD64.EXE` and `19` `*.EXE` utilities
- `build/{fasmarm,llvm}/arm32/` — `WINCMD_ARMV4.EXE`, `WINCMD_ARM32.EXE`, and `19` `*.EXE` utilities
- `build/{fasmarm,llvm}/arm64/` — `WINCMD_ARM64.EXE` and `19` `*.EXE` utilities

### 2.2 Native Windows (`NT 3.1` – `Windows 11`) via `make.bat` / `BUILD.CMD`

```bat
REM Universal entrypoint (dispatches to BUILD.CMD on Windows NT, BUILD.BAT on MS-DOS)
make.bat all

REM Toolchain-specific or maintenance commands on Windows NT..11
BUILD.CMD nasm
BUILD.CMD fasm
BUILD.CMD jwasm
BUILD.CMD hooks
BUILD.CMD clean
```

Self-hosting verification on Windows (running `BUILD.CMD` or `tests\SELFHOST.CMD` **inside** `WINCMD64.EXE` or `WINCMD_ARM64.EXE`):

```bat
"build/nasm/x86_64/WINCMD64.EXE" /C tests\SELFHOST.CMD
```

### 2.3 Native MS-DOS (`2.00` / `4.00` / `7.x`) Self-Hosting via `BUILD.BAT` & `ASMCONV.COM`

Inside real MS-DOS or FreeDOS with `NASM.EXE`, `FASM.EXE`, or `JWASMR.EXE` in `PATH`:

```bat
REM Convert a canonical .ASM module directly inside 16-bit DOS (/N = NASM, /F = FASM, /J = JWasm)
ASMCONV /N ASM\DOS16\WINCMD.ASM WINCMDN.ASM

REM Assemble the DOS16 shell + all 19 .COM utilities natively inside MS-DOS
BUILD NASM
BUILD FASM
BUILD JWASM
BUILD CLEAN
```

### 2.4 Zero-Rust Standalone Release Tarballs

`make package` produces three pre-lowered source tarballs in `dist/release/` requiring **zero Cargo/Rust installation**:
- `wincmd-src-nasm.tar.gz`
- `wincmd-src-fasm.tar.gz`
- `wincmd-src-jwasm.tar.gz`

```bash
tar -xzf dist/release/wincmd-src-fasm.tar.gz
cd wincmd-src-fasm
sh ./build-standalone.sh
```

---

## 3. Shell Executables & Startup Switches Reference

### 3.1 MS-DOS (`WINCMD.COM`, `WINCMD1.COM`, `WINCMDMZ.EXE`)

**Primary Boot Shell (`CONFIG.SYS` on MS-DOS `2.00+` / FreeDOS)**:

```ini
FILES=20
BUFFERS=15
SHELL=C:\WINCMD.COM C:\ /P /E:1024 /MSG
```

**Command-Line Syntax**:

```text
WINCMD [drive:path] [device] [/P] [/E:nnnnn] [/MSG] [/Y] [/F] [/D] [/Q] [/V:ON|OFF] [{/C | /K} string]
```

| Switch | Description |
| :--- | :--- |
| `[drive:path]` | Sets initial `%COMSPEC%` directory used to reload the `TRANSIENT` module if overwritten by large child apps. |
| `[device]` | Character device for console I/O (e.g., `AUX`, `COM1`), equivalent to startup `CTTY`. |
| `/P` | Permanent primary shell mode: disables `EXIT` termination and runs `\AUTOEXEC.BAT` (or prompts for `DATE`/`TIME` if absent, unless `/D` is given). |
| `/C string` | Executes `string` and terminates with its `ERRORLEVEL`. |
| `/K string` | Executes `string` and enters the interactive prompt. |
| `/E:nnnnn` | Environment block size in bytes (`160..32768`, rounded up to 16-byte paragraphs) or `/E:ON` / `/E:OFF` command extensions toggle. |
| `/MSG` | Retains `WINCMD.MSG` extended DOS `4.00+` error descriptions in memory so floppy media need not stay inserted. |
| `/F` | Auto-selects **Fail** on `INT 24h` hardware critical errors instead of prompting `Abort, Retry, Ignore, Fail?`. |
| `/D` | Skips `AUTOEXEC.BAT` execution under `/P`. |
| `/Q` | Turns command `ECHO OFF` initially. |
| `/V:ON\|OFF` | Enables or disables `!VAR!` delayed environment variable expansion. |
| `/Y` | Steps through `.BAT` execution line-by-line with `[Y/N]` confirmation (used with `/C` or `/K`). |

### 3.2 16-Bit Windows (`WINCMD16.EXE`, `WINCMD286.EXE`, `WINCMD.PIF`)

- **`WINCMD16.EXE`**: Universal 8086 Real/Protected-Mode `NE` GUI terminal (`Windows 1.01` – `Windows 3.2`).
- **`WINCMD286.EXE`**: 80286 Protected-Mode `NE` GUI terminal (`Windows/286`, `3.0` Standard/Enhanced, `3.1`, `WfW 3.11`, `3.2`) with discardable `_TRN286` code segment.
- **`WINCMD.PIF`**: Place alongside `WINCMD.COM` so `WINOLDAP` launches external 16-bit DOS binaries with `640 KiB` conventional memory limit, windowed text mode (`386 Enhanced`), background execution, and idle slice detection.

```text
WINCMD16 [/Q] [/F] [--internal-dos] [{/C | /K} command]
WINCMD286 [/Q] [/F] [--internal-dos] [{/C | /K} command]
```

### 3.3 32/64-Bit Windows & Windows CE (`WINCMD32`, `WINCMD64`, `WINCMD_ARMV4`, `WINCMD_ARM32`, `WINCMD_ARM64`)

```text
WINCMD64 [/A | /U] [/Q] [/D] [/E:ON|OFF] [/F:ON|OFF] [/V:ON|OFF] [/T:bf] [[/S] {/C | /K} string]
```

| Switch | Description |
| :--- | :--- |
| `/C string` | Carries out the command specified by `string` and exits with its `ERRORLEVEL`. |
| `/K string` | Carries out `string` and remains in interactive mode. |
| `/S` | Modifies quote stripping after `/C` or `/K` (always strips outer `"..."` quotes, matching `CMD.EXE` 5-rule quote heuristic). |
| `/A` / `/U` | Formats internal command output routed to pipes or files as ANSI/WTF-8 (`/A`, default) or `UTF-16LE` (`/U`). |
| `/Q` | Turns initial `ECHO` off. |
| `/D` | Disables execution of `HKLM` / `HKCU` `...\Command Processor\AutoRun` Registry commands at startup. |
| `/E:ON\|OFF` | Enables (`ON`, default, `CMDEXTVERSION=2`) or disables (`OFF`) Command Extensions. |
| `/F:ON\|OFF` | Enables or disables `Tab` / `Ctrl+D` / `Ctrl+F` filename and directory completion. |
| `/V:ON\|OFF` | Enables (`ON`) or disables (`OFF`, default) `!VAR!` delayed environment variable expansion. |
| `/T:bf` | Sets console background (`b`) and foreground (`f`) hex color attributes (`0`–`F`, matching `COLOR`). |
| `--vm8086` | *(Diagnostic/Override)* Forces external `.COM`/`.EXE` targets through the in-process `vm8086` 16-bit DOS virtual CPU. |
| `--win9x-ansi-sim` | *(`WINCMD32` only)* Forces the Windows 95/98/Me ANSI `*A` HAL vtable on NT/Wine hosts for regression testing. |
| `--nt31-api-cap` | *(`WINCMD32` only)* Masks post-NT-3.1 dynamic APIs to test Windows NT 3.1 compatibility paths. |
| `--nt52-api-cap` | *(`WINCMD64` only)* Caps runtime OS APIs at Windows XP Pro x64 / Server 2003 SP1 (`NT 5.2`). |

---

## 4. Interactive Line Editing, `DOSKEY` & `PROMPT` Reference

### Interactive Editing Keys
| Key(s) | Action |
| :--- | :--- |
| `Left` / `Right`, `Home` / `End` | Move cursor by character (DBCS/UTF-8 codepoint-aware) or jump to start/end of line. |
| `Up` / `Down` | Cycle through the 64-entry command history ring. |
| `Ins` / `Del` / `Backspace` / `Esc` | Toggle insert/overwrite mode, delete char at/before cursor, or clear current input line. |
| `F1` / `F3` / `F5` | Copy one character / copy remainder from previous template / copy current line into template. |
| `F7` / `Alt+F7` / `F8` / `F9` | Display numbered history list / clear history / prefix-match history search / select by entry number. |
| `Tab` / `Shift+Tab` | Cycle forward/backward through matching files & directories (auto-quoting names containing spaces). |
| `Ctrl+C` / `Ctrl+Break` | Cancel current input line or interrupt running batch script (`Terminate batch job (Y/N)?`). |

### `PROMPT` Escape Codes
Configure via `PROMPT <text>` (default `$P$G`, e.g. `C:\>`):
- `$P` (current drive & path), `$N` (current drive), `$G` (`>`), `$L` (`<`), `$B` (`|`), `$Q` (`=`), `$$` (`$`), `$D` (current date), `$T` (current time), `$V` (OS/shell version), `$_` (`CRLF` newline), `$E` (`0x1B` ESC for ANSI/VT color sequences), `$H` (backspace), `$S` (space), `$A` (`&`), `$C` (`(`), `$F` (`)`), `$+` (one `+` per `PUSHD` directory-stack depth).

---

## 5. Batch Scripting Language, Expansion & Operators Guide

### 5.1 Pipelines, Redirection & Conditional Chaining

```bat
REM Sequential, short-circuit AND (&& on ERRORLEVEL 0), short-circuit OR (|| on non-zero)
MD build 2>NUL & COPY src.txt build\dst.txt >NUL && ECHO Copy succeeded || ECHO Copy failed

REM Concurrent CreatePipe pipeline (NT/Win9x/ARM) / spooled pipeline (DOS/WinCE)
DIR /B | FINDSTR /R "^WINCMD" | SORT /R > matches.txt 2>&1

REM Parenthesized multi-line compound block with single unified redirection
(
    ECHO Header
    VER
    HOSTNAME
) > report.txt
```

Supported redirection operators on handles `0..9`: `< file` (stdin), `> file` (truncate stdout), `>> file` (append stdout), `2> file` (stderr), `2>&1` (merge stderr into stdout), `1>&2` (route stdout to stderr).

### 5.2 Variables, Substrings, Replacements & Delayed Expansion (`!VAR!`)

```bat
@ECHO OFF
SETLOCAL EnableDelayedExpansion
SET "STR=hello_world_2026"

REM Substring slicing: %VAR:~start,length% (negative offsets count from end)
ECHO First 5: %STR:~0,5%
ECHO Year:    %STR:~-4%
ECHO Trim 5:  %STR:~0,-5%

REM Search & replace (case-insensitive) and prefix strip (*old=new)
ECHO Hyphen:  %STR:_=-%
ECHO Tail:    %STR:*world_=%

REM Delayed !VAR! inside a parenthesized FOR / IF block
SET /A TOTAL=0
FOR /L %%N IN (1,1,5) DO (
    SET /A TOTAL+=%%N
    ECHO Step %%N running total = !TOTAL!
)
ENDLOCAL
```

**Dynamic Pseudo-Variables** (evaluated on demand unless overridden via `SET`):
`%CD%`, `%DATE%`, `%TIME%`, `%RANDOM%` (`0..32767`), `%ERRORLEVEL%`, `%CMDEXTVERSION%` (`2`), `%CMDCMDLINE%`, `%HIGHESTNUMANODENUMBER%`.

### 5.3 Batch Parameters & `%~` Path/Metadata Modifiers

Applicable to batch arguments (`%0`–`%9`) and `FOR` loop variables (`%%I`):

| Modifier | Expands To |
| :--- | :--- |
| `%~I` | `I` with surrounding `"` quotes stripped |
| `%~fI` | Fully qualified canonical path |
| `%~dI` | Drive letter with colon (e.g. `C:`) |
| `%~pI` | Path only (with trailing separator) |
| `%~nI` | Filename stem without extension |
| `%~xI` | File extension including leading `.` |
| `%~sI` | Short `8.3` path components |
| `%~aI` | File attribute string (`--a------`, `d--------`) |
| `%~tI` | File last-write date and time |
| `%~zI` | Exact file size in bytes (64-bit safe) |
| `%~$PATH:I` | Searches directories in `%PATH%` (or any `%ENV%`) and expands to first matching full path |
| `%~dp0` / `%~dpnx1` | Composed modifiers (e.g. script's own directory `%~dp0`) |

### 5.4 `SET /A` 11-Tier Signed 32-Bit Arithmetic

```bat
SET /A "A=0x10, B=010, C=(A + B) * 3"
SET /A "MASK=(1 << 8) - 1, C &= MASK"
ECHO A=%A% B=%B% C=%C%
```

Supports `()`, unary `+ - ! ~`, multiplicative `* / %`, additive `+ -`, shifts `<< >>`, bitwise `& ^ |`, compound assignments `= *= /= %= += -= &= ^= |= <<= >>=`, and comma `,` expressions. Always quote expressions containing `&`, `|`, `^`, `<`, or `>` (`SET /A "X=A & 0xFF"`).

### 5.5 `IF`, `FOR` (`/D`, `/R`, `/L`, `/F`), `CALL :label` & `SETLOCAL`

```bat
@ECHO OFF
SETLOCAL EnableExtensions

REM Case-insensitive numeric & string IF / ELSE
IF /I "%1"=="clean" (
    ECHO Cleaning...
) ELSE IF %ERRORLEVEL% GEQ 1 (
    ECHO Previous error detected
)

REM FOR /L numeric sequence: (start, step, end)
FOR /L %%I IN (0, 2, 6) DO ECHO Even=%%I

REM FOR /D directories & FOR /R recursive walk
FOR /D %%D IN (asm\*) DO ECHO TargetDir=%%D
FOR /R asm\inc %%F IN (*.inc) DO ECHO Header=%%~nxF

REM FOR /F file, 'string', and `command` token parser
FOR /F "tokens=1,2* delims=: " %%A IN ('"VER"') DO ECHO Col1=%%A Col2=%%B Rest=%%C

REM Subroutine CALL :label + SETLOCAL/ENDLOCAL return-value tunneling
CALL :COMPUTE 21 RESULT
ECHO Result=%RESULT%
EXIT /B 0

:COMPUTE
SETLOCAL
SET /A "TMP=%~1 * 2"
ENDLOCAL & SET "%~2=%TMP%"
EXIT /B 0
```

---

## 6. Complete 47 Internal Built-In Commands Reference

| Command | Syntax & Key Switches | Description |
| :--- | :--- | :--- |
| **`ASSOC`** | `ASSOC [.ext[=[fileType]]]` | Displays or modifies `HKCR` file extension associations (`NT`/Win32/64/ARM). |
| **`BREAK`** | `BREAK [ON \| OFF]` | Sets or displays extended `Ctrl+C` checking on MS-DOS (compatibility no-op on NT). |
| **`CALL`** | `CALL [drive:][path]file [args]` / `CALL :label [args]` | Calls another batch script or internal `:label` subroutine without terminating caller. |
| **`CD` / `CHDIR`** | `CD [/D] [drive:][path]` | Displays or changes current directory (`/D` also switches active drive). |
| **`CHCP`** | `CHCP [nnn]` | Displays or sets active console/NLS OEM codepage (e.g. `437`, `850`, `936`, `65001`). |
| **`CLS`** | `CLS` | Clears console screen buffer or Win16 GDI viewport and homes cursor. |
| **`CMD` / `COMMAND`** | `CMD [/A\|/U] [/Q] [/D] [/E:ON\|OFF] [/V:ON\|OFF] [{/C\|/K} str]` | Starts a nested instance of the active `wincmd` shell. |
| **`COLOR`** | `COLOR [attr]` | Sets console background/foreground hex colors (e.g. `COLOR 0A`; `COLOR` resets default). |
| **`COPY`** | `COPY [/A \| /B] [/V] [/Y \| /-Y] src[+src2...] [dst]` | Copies single files, wildcards, or `+`-concatenated streams (`/A` Ctrl+Z text, `/B` raw binary). |
| **`CTTY`** | `CTTY device` | Redirects MS-DOS standard console I/O to `AUX`, `COM1`–`COM4`, or `CON`. |
| **`DATE`** | `DATE [/T] [date]` | Displays or sets system date (`/T` prints without prompting). |
| **`DEL` / `ERASE`** | `DEL [/P] [/F] [/S] [/Q] [/A[[:]attrs]] names` | Deletes one or more files (`/F` read-only, `/S` recursive, `/Q` quiet, `/A` attribute filter). |
| **`DIR`** | `DIR [path] [/A[attrs]] [/B] [/C] [/D] [/L] [/N] [/O[sort]] [/P] [/Q] [/R] [/S] [/T[time]] [/W] [/X] [/4]` | Lists files, `<DIR>`/`<JUNCTION>`/`<SYMLINKD>` entries, short names (`/X`), and 64-bit free bytes. |
| **`DOSKEY`** | `DOSKEY [/HISTORY] [/REINSTALL] [/LISTSIZE=n] [mac=text]` | Manages command-line history and interactive `$1..$9`/`$*`/`$T` macros. |
| **`DPATH`** | `DPATH [path;...]` | Sets or displays the `%DPATH%` data-file search path. |
| **`ECHO`** | `ECHO [ON \| OFF]` / `ECHO [message]` / `ECHO.` | Toggles command echoing, prints text, or emits a blank line (`ECHO.`). |
| **`ENDLOCAL`** | `ENDLOCAL` | Restores environment, CWD, drive, and delayed-expansion state from matching `SETLOCAL`. |
| **`EXIT`** | `EXIT [/B] [exitCode]` | Exits current batch script/subroutine (`/B`) or shell process (`exitCode` sets `ERRORLEVEL`). |
| **`FOR`** | `FOR [/D \| /R [path] \| /L \| /F ["opts"]] %%V IN (set) DO cmd` | Iterates files, directories, directory trees, integer ranges, or tokenized lines/command output. |
| **`FTYPE`** | `FTYPE [fileType[=[openCmdString]]]` | Displays or modifies `HKCR\<type>\shell\open\command` launch templates. |
| **`GOTO`** | `GOTO [:]label` / `GOTO :EOF` | Jumps batch execution to `:label` (or exits current script/subroutine on `:EOF`). |
| **`IF`** | `IF [NOT] [/I] cond cmd [ELSE cmd]` | Conditional execution (`ERRORLEVEL`, `EXIST`, `DEFINED`, `CMDEXTVERSION`, `==`, `EQU`..`GEQ`). |
| **`KEYS`** | `KEYS [ON \| OFF]` | Enables/queries command-line editing state. |
| **`LH` / `LOADHIGH`** | `LH [drive:][path]prog [args]` | Loads TSR/program into Upper Memory Blocks (`UMB`) on DOS `5.0+` (transparent spawn elsewhere). |
| **`MD` / `MKDIR`** | `MD [drive:]path` | Creates directories (creates intermediate parent directories when `/E:ON` is active). |
| **`MKLINK`** | `MKLINK [[/D] \| [/H] \| [/J]] Link Target` | Creates file symlinks, `/D` directory symlinks, `/H` hard links, or `/J` NTFS directory junctions. |
| **`MODE`** | `MODE [CON[:] [COLS=c] [LINES=n]]` / `MODE COMn:...` | Configures or displays console dimensions, serial port parameters, or codepage status. |
| **`MOVE`** | `MOVE [/Y \| /-Y] src dst` | Moves files or renames directories across paths/volumes (`MoveFileExW` / copy+unlink fallback). |
| **`PATH`** | `PATH [[drive:]path[;...]]` / `PATH ;` | Displays, updates, or clears (`PATH ;`) the executable search `%PATH%`. |
| **`PAUSE`** | `PAUSE` | Prints `Press any key to continue . . .` and waits for a keystroke. |
| **`POPD`** | `POPD` | Pops directory saved by `PUSHD` (and unmaps temporary UNC drive letters). |
| **`PROMPT`** | `PROMPT [text]` | Sets the interactive `%PROMPT%` template string. |
| **`PUSHD`** | `PUSHD [path \| //server/share]` | Pushes current directory onto stack and switches to `path` (auto-mapping UNC network shares via `WNet`). |
| **`RD` / `RMDIR`** | `RD [/S] [/Q] [drive:]path` | Removes directories; `/S` recursively deletes trees while **safely unlinking junctions/symlinks without traversing targets**. |
| **`REM`** | `REM [comment]` | No-op batch comment statement. |
| **`REN` / `RENAME`** | `REN [drive:][path]src dst` | Renames files or directories in place (supports wildcards). |
| **`SET`** | `SET [var=[val]]` / `SET /A expr` / `SET /P var=[prompt]` | Lists prefix-matched variables, assigns/deletes (`SET var=`), evaluates `/A` math, or reads `/P` user input. |
| **`SETLOCAL`** | `SETLOCAL [EnableExtensions\|DisableExtensions] [EnableDelayedExpansion\|DisableDelayedExpansion]` | Begins localized scope (up to 32 nested levels) for environment variables and working directory. |
| **`SHIFT`** | `SHIFT [/n]` | Shifts batch positional parameters (`%n+1 -> %n`, leaving `%0..%n-1` intact with `/n`). |
| **`START`** | `START ["title"] [/D path] [/B] [/WAIT] [/MIN\|/MAX] [cmd] [args]` | Launches a program, batch script, document/URL (`ShellExecuteExW`), or new console window. |
| **`TIME`** | `TIME [/T] [time]` | Displays or sets system clock (`/T` prints without prompting). |
| **`TITLE`** | `TITLE [string]` | Sets the console window or Win16 terminal title bar text. |
| **`TRUENAME`** | `TRUENAME [path]` | Resolves relative, `SUBST`, UNC, or symlink/junction paths to canonical physical form. |
| **`TYPE`** | `TYPE [drive:][path]filename [...]` | Streams file contents to `stdout` (BOM-aware for UTF-8 / UTF-16LE on PE shells). |
| **`VER`** | `VER` | Prints shell architecture and host OS version string, resetting `ERRORLEVEL` to `0`. |
| **`VERIFY`** | `VERIFY [ON \| OFF]` | Toggles or displays MS-DOS disk-write verification flag. |
| **`VOL`** | `VOL [drive:]` | Displays volume label and 32-bit hex serial number (`XXXX-XXXX`). |

---

## 7. Running 16-Bit DOS `.COM` & `MZ` Programs on 64-Bit & ARM Windows (`vm8086`)

On 64-bit x64 (`WINCMD64.EXE`), ARM32 (`WINCMD_ARMV4.EXE`, `WINCMD_ARM32.EXE`), and ARM64 (`WINCMD_ARM64.EXE`) — where Windows cannot natively execute 16-bit binaries — `wincmd` automatically intercepts `.COM` and 16-bit DOS `MZ` `.EXE` invocations and executes them in-process inside its embedded `vm8086` engine:

```bat
REM Inside WINCMD64.EXE on 64-bit Windows 11 x64 or WINCMD_ARM64.EXE on Windows 11 ARM64:
REM Directly invoke real 16-bit 8086 .COM binaries and pipe into native 64-bit utilities!
"build/nasm/dos16/HOSTNAME.COM" | "build/nasm/x86_64/FINDSTR.EXE" /I "."
ECHO ExitCode=%ERRORLEVEL%

REM Run the 8086 self-hosting lowerer ASMCONV.COM directly on 64-bit ARM64/x64 Windows:
"build/nasm/dos16/ASMCONV.COM" /N asm\dos16\wincmd_m.asm out_msg.asm
```

Guest 16-bit programs inherit the shell's active working directory, environment variables (`<= 32 KiB` DOS block), and redirected `stdin`/`stdout`/`stderr` pipe/file handles, and return their exact `INT 21h AH=4Ch` exit code as `%ERRORLEVEL%`.

---

## 8. Complete 19 Companion Utilities Reference (`*.COM` & `*.EXE`)

Every utility below is built both as a 16-bit MS-DOS `.COM` (`build/<tc>/dos16/*.COM`) and as native 32/64-bit CRT-free `PE32`/`PE32+` executables (`build/<tc>/{x86_32,x86_64,arm32,arm64}/*.EXE`). All accept `/?` for built-in usage help.

| Utility | Syntax & Supported Switches | Exit Codes & Notes |
| :--- | :--- | :--- |
| **`ATTRIB`** | `ATTRIB [+R\|-R] [+A\|-A] [+S\|-S] [+H\|-H] [+I\|-I] [+O\|-O] [+P\|-P] [+U\|-U] [path] [/S] [/D] [/L]` | `0` ok, `1` error. Displays or modifies standard + Win10/11 cloud/pinned attributes (`/L` acts on symlink itself). |
| **`CHOICE`** | `CHOICE [/C[:]choices] [/N] [/CS] [/T nn /D c] [/M "text"]` | Returns `1..N` (`1`-based index into `choices`, default `YN`), `255` on invalid switch/error, `0` on `Ctrl+C`. |
| **`COMP`** | `COMP [data1] [data2] [/D] [/A] [/L] [/N=number] [/C] [/M]` | `0` identical, `1` differences found, `2` file/syntax error. Fixed-offset byte/line comparator (`/D` decimal, `/A` ASCII). |
| **`FC`** | `FC [/A] [/B] [/C] [/L] [/LBn] [/N] [/OFF[LINE]] [/T] [/U] [/W] [/nnnn] file1 file2` | `0` identical, `1` different, `2` missing file, `-1` bad syntax. Resynchronizing line diff or `/B` hex byte diff. |
| **`FIND`** | `FIND [/V] [/C] [/N] [/I] [/OFF[LINE]] "string" [file ...]` | `0` match found, `1` no match, `2` error. UTF-8/UTF-16 BOM-aware substring filter (`stdin` when no file given). |
| **`FINDSTR`** | `FINDSTR [/B] [/E] [/L] [/R] [/S] [/I] [/X] [/V] [/N] [/M] [/O] [/P] [/C:"str"] [/G:f] [/F:f] strings [files...]` | `0` match found, `1` no match, `2` syntax/open error. Non-backtracking 64-state bitset Thompson NFA regex engine. |
| **`HOSTNAME`** | `HOSTNAME` | `0` ok. Prints current NetBIOS/DNS host name (`GetComputerNameW` on Windows; `%COMPUTERNAME%`/`DOSPC` on DOS). |
| **`MEM`** | `MEM [/C \| /D \| /F \| /M module \| /P]` | `0` ok. Reports conventional/UMB/EMS/XMS MCB layout on DOS; 64-bit physical/pagefile/virtual memory (`GlobalMemoryStatusEx`) on PE. |
| **`MORE`** | `MORE [/E] [/C] [/P] [/S] [/Tn] [+n] [files...]` | `0` ok. Interactive console pager (`Space` next page, `Enter` next line, `Q` quit; auto-passthrough when stdout is redirected). |
| **`REG`** | `REG <QUERY\|ADD\|DELETE\|COPY\|EXPORT\|IMPORT\|SAVE\|RESTORE\|COMPARE\|FLAGS> KeyName [options]` | `0` success, `1` failure. Supports `HKLM`/`HKCU`/`HKCR`/`HKU`/`HKCC`, `/v`, `/ve`, `/s`, `/t REG_SZ\|REG_DWORD\|REG_QWORD\|REG_EXPAND_SZ\|REG_MULTI_SZ\|REG_BINARY`, `/d`, `/f`. |
| **`REPLACE`** | `REPLACE [drive1:][path1]filename [drive2:][path2] [/A] [/P] [/R] [/S] [/W] [/U]` | `0` ok, `2` source not found, `3` path not found, `11` bad syntax. Adds (`/A`) or updates (`/S`, `/U`) existing files. |
| **`ROBOCOPY`** | `ROBOCOPY src dst [file...] [/S] [/E] [/LEV:n] [/PURGE] [/MIR] [/MOV] [/MOVE] [/MT[:n]] [/L] [/XF f...] [/XD d...] [/R:n] [/W:n] [/NP] [/NFL] [/NDL]` | Authentic bitmask exit code: `0` no changes, `1` files copied, `2` extras detected/purged, `3` (`1\|2`), `>= 8` copy failure, `16` fatal usage error. Multi-threaded `/MT[:n]` on PE! |
| **`SORT`** | `SORT [/R] [/+n] [/M kb] [/L loc] [/REC bytes] [/O output] [/UNIQUE] [file]` | `0` ok, `1` error. `O(N log N)` merge sort (`/R` descending, `/+n` 1-based start column, `/UNIQUE` deduplicate). |
| **`TIMEOUT`** | `TIMEOUT [/T] seconds [/NOBREAK]` | `0` countdown/key complete, `1` error/abort. Waits `0..99999` seconds (`/NOBREAK` ignores keys except `Ctrl+C`). |
| **`TREE`** | `TREE [drive:][path] [/F] [/A]` | `0` ok. Renders recursive directory hierarchy using CP437 box drawing or `/A` pure ASCII (`+---` / `\---`), plus `/F` filenames. |
| **`TRUENAME`** | `TRUENAME [path]` | `0` ok, `1` error. Resolves canonical full path and NTFS symlink/junction target (`GetFinalPathNameByHandleW` / `INT 21h AH=60h`). |
| **`WHERE`** | `WHERE [/R dir] [/Q] [/F] [/T] [$ENV:]pattern [...]` | `0` found, `1` not found, `2` error. Searches `%PATH%` + `PATHEXT`, `$ENV:pattern`, or recursive `/R dir` (`/Q` quiet, `/T` size/timestamp). |
| **`WHOAMI`** | `WHOAMI [/USER] [/GROUPS] [/PRIV] [/LOGONID] [/ALL] [/UPN] [/FQDN] [/FO TABLE\|LIST\|CSV] [/NH]` | `0` ok, `1` error. Queries process access token `DOMAIN\User`, binary `SID` (`S-1-5-...`), group memberships, and privileges. |
| **`XCOPY`** | `XCOPY src [dst] [/A] [/M] [/D[:m-d-y]] [/S] [/E] [/I] [/Q] [/F] [/L] [/H] [/R] [/T] [/U] [/K] [/Y \| /-Y] [/EXCLUDE:f1[+f2]]` | `0` ok, `1` no files found, `4` init/syntax error. Recursive directory & attribute-preserving copier. |

### Practical Companion Utility Examples

```bat
REM Search all canonical assembly files for @proc_frame using Thompson NFA regex
FINDSTR /S /N /R "^@proc_frame" asm\*.asm

REM Multi-threaded 16-thread directory mirror dry-run (/L), excluding *.tmp
ROBOCOPY src backup /MIR /MT:16 /XF *.tmp /L

REM Query and write a test key under HKCU via pure-assembly REG.EXE
REG ADD HKCU\Software\WinCmdTest /v Build /t REG_DWORD /d 42 /f
REG QUERY HKCU\Software\WinCmdTest /v Build
REG DELETE HKCU\Software\WinCmdTest /f

REM Display current user account + SID in CSV format without header
WHOAMI /USER /FO CSV /NH
```

---

## 9. Rust CLI Verification, Emulation, Fuzzing & Packaging Tools

### `asm-conv` — Canonical Assembly Lowerer
```bash
# Lower entire asm/ tree into all 6 assembler backends (plus fasm-pe32 & fasm-coff)
cargo run --release -p asm-conv -- --in-dir asm --out-dir dist/src

# Lower a single canonical file to a specific assembler (nasm|fasm|jwasm|fasmarm|llvm-mc|armasm)
cargo run --release -p asm-conv -- --in-file asm/x86_64/wincmd64.asm --out-file /tmp/wincmd64.nasm.asm --target nasm
```

### `asm-lint` — Static Doc, CFG, ABI, Purity, Equivalence & Budget Verifier
```bash
# Lint all 239 canonical assembly modules
cargo run --release -p asm-lint -- --dir asm

# Check cross-assembler binary or PE .text equivalence
cargo run --release -p asm-lint -- --equiv WINCMD.COM build/nasm/dos16/WINCMD.COM build/fasm/dos16/WINCMD.COM build/jwasm/dos16/WINCMD.COM

# Disassemble & verify ISA opcode purity (8086 | 286 | 386 | x64 | armv4 | armv7 | arm64)
cargo run --release -p asm-lint -- --purity arm64 build/fasmarm/arm64/WINCMD_ARM64.EXE

# Enforce maximum binary size in bytes
cargo run --release -p asm-lint -- --budget build/nasm/dos16/WINCMD.COM 49152
```

### `emu-cov` — Deterministic 5-ISA Emulator & LCOV Trace Generator
```bash
# Emulate 16-bit DOS .COM across eras (1.25 | 2.00 | 4.00 | 7.10) and emit lcov.info
cargo run --release -p emu-cov -- --com build/nasm/dos16/WINCMD.COM --era 4.00 --args "/C VER" --lcov lcov.dos.info

# Emulate IA-32, x86-64, ARMv4, ARMv7-Thumb2, or ARM64 PE executables directly on any host!
cargo run --release -p emu-cov -- --pe build/nasm/x86_32/WINCMD32.EXE --args "/C VER"
cargo run --release -p emu-cov -- --pe build/nasm/x86_64/WINCMD64.EXE --args "/C VER"
cargo run --release -p emu-cov -- --pe build/fasmarm/arm32/WINCMD_ARMV4.EXE --args "/C VER"
cargo run --release -p emu-cov -- --pe build/fasmarm/arm32/WINCMD_ARM32.EXE --args "/C VER"
cargo run --release -p emu-cov -- --pe build/fasmarm/arm64/WINCMD_ARM64.EXE --args "/C VER" --lcov lcov.a64.info
```

### `diff-fuzz` — Deterministic Cross-Assembler, 7-ISA & Reference `cmd.exe` Fuzzer
```bash
# Cross-assembler 8086 DOS differential fuzzing across NASM, FASM, and JWasm
cargo run --release -p diff-fuzz -- --seed 0x57494E434D44 --cases 2500 --era 4.00 build/nasm/dos16/WINCMD.COM build/fasm/dos16/WINCMD.COM build/jwasm/dos16/WINCMD.COM

# 7-target Cross-ISA fuzzing (8086, Win16 NE, IA-32, x64, ARMv4, ARMv7, ARM64) + live cmd.exe parity
cargo run --release -p diff-fuzz -- --cross-isa --seed 0x20261123 --cases 10000 --ref-cmd 25 --build-dir build
```

### `media-pkg` — Reproducible Floppy, ISO-9660, ZIP & Tarball Masterer
```bash
cargo run --release -p media-pkg -- --root . --build-dir build --dist-src dist/src --out-dir dist/release
```

---

## 10. Booting Mastered Media (`.IMG` / `.ISO`) & Replicating CI

### Mastered Artifacts in `dist/release/`
| File | Size | Contents |
| :--- | :--- | :--- |
| `wincmd-dos-160k.img` | `160 KiB` | FAT12 5.25" single-sided floppy: `WINCMD.COM`, `WINCMD1.COM`, `WINCMD.MSG`, `ASMCONV.COM` + all `19` `.COM` utilities. |
| `wincmd-dos-360k.img` | `360 KiB` | Adds relocatable `WINCMDMZ.EXE`. |
| `wincmd-dos-win16-720k.img` | `720 KiB` | Adds `WINCMD16.EXE`, `WCMD286.EXE` (`WINCMD286.EXE`), and `WINCMD.PIF`. |
| `wincmd-full-1440k.img` | `1.44 MiB` | Adds `WINCMD32.EXE`, `WINCMD64.EXE`, `WCMDARM4.EXE`, `WCMDARM7.EXE`, and `WCMDA64.EXE`. |
| `wincmd-win-universal.iso` | `~1.3 MiB` | ISO-9660 universal CD-ROM containing `DOS16/`, `WIN16/`, `WIN32/`, `WIN64/`, `ARM32/`, `ARM64/`, scripts, and docs. |
| `wincmd-{dos-win16,win32-x86,win64-x64,win-arm32,win-arm64}.zip` | `93–291 KiB` | Per-platform binary + companion-utility ZIP distributions. |
| `wincmd-src-{nasm,fasm,jwasm}.tar.gz` | `2.5–6.6 MiB` | Zero-Rust standalone pre-lowered source tarballs. |
| `SHA256SUMS.txt` | `1,146 B` | Deterministic `SHA-256` manifest across all 13 artifacts. |

### Attaching Media to QEMU / Virtual Machines
```bash
# Attach the 1.44M floppy image as drive B: alongside a bootable DOS/FreeDOS image on drive A:
qemu-system-i386 -fda freedos.img -fdb dist/release/wincmd-full-1440k.img -m 16 -boot a

# Attach the universal ISO-9660 CD-ROM to any Windows 95 / NT 4.0 / 2000 / XP / 10 / 11 VM
qemu-system-x86_64 -cdrom dist/release/wincmd-win-universal.iso -hda win_disk.qcow2 -m 2048
```

### Running the Full CI & Real QEMU MS-DOS Boot Matrix Locally
```bash
# Verify CRLF (.bat/.cmd/.pif/.reg) and LF (.asm/.inc/.rs/.md/.sh) line-ending compliance
sh ./ci/check_eol.sh

# Replicate host CI stages (eol, rust-fmt-clippy-doc, build-all, coverage) without GitHub Actions
make ci-local

# Replicate Ubuntu & Alpine Linux container CI jobs via Docker/Podman
make ci-docker

# Boot real MS-DOS (1.25, 2.00, 4.00) and FreeDOS 1.3 inside headless QEMU via Bento
BENTO_DIR=../bento sh ./ci/run_dos_qemu_matrix.sh --os all --toolchain nasm
```
