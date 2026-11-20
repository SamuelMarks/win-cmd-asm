@ECHO OFF
SETLOCAL ENABLEEXTENSIONS DISABLEDELAYEDEXPANSION
REM ============================================================================
REM win-cmd-asm — Pre-Commit Hook (Raw Windows NT .cmd Script, Zero Python)
REM
REM Performs:
REM   1. dos2unix / unix2dos encoding & line-ending enforcement
REM   2. Multi-ISA static assembly + Rust lints (asm-lint, clippy, rustdoc)
REM   3. Multi-ISA, multi-assembler build & binary equivalence/purity/budget
REM   4. Unit, integration, 8086 emulator coverage, and differential fuzz tests
REM   5. Dynamic '% doc coverage' shield add/update in README.md
REM   6. Dynamic '% test coverage' shield add/update in README.md
REM ============================================================================

SET "SELF_PATH=%~f0"
SET "ARG_FIRST=%~1"

FOR /F "delims=" %%R IN ('git rev-parse --show-toplevel 2^>NUL') DO SET "REPO_ROOT=%%R"
IF NOT DEFINED REPO_ROOT (
    ECHO [pre-commit] ERROR: Unable to determine git repository root. 1>&2
    EXIT /B 1
)
PUSHD "%REPO_ROOT%" || EXIT /B 1

SET "HOOK_TMP=%TEMP%\wincmd_precommit_%RANDOM%_%RANDOM%"
MKDIR "%HOOK_TMP%" || (
    POPD
    EXIT /B 1
)

REM ----------------------------------------------------------------------------
REM 1. Enforce Correct Encoding (dos2unix / unix2dos)
REM ----------------------------------------------------------------------------
ECHO [pre-commit] 1/6 Enforcing line-ending encodings via dos2unix and unix2dos...

WHERE dos2unix >NUL 2>NUL
IF ERRORLEVEL 1 (
    ECHO [pre-commit] ERROR: 'dos2unix' is required in PATH. 1>&2
    GOTO :FAIL
)
WHERE unix2dos >NUL 2>NUL
IF ERRORLEVEL 1 (
    ECHO [pre-commit] ERROR: 'unix2dos' is required in PATH. 1>&2
    GOTO :FAIL
)

git diff --cached --name-only --diff-filter=ACMR > "%HOOK_TMP%\staged.lst"
SET "HAS_STAGED=0"
FOR %%S IN ("%HOOK_TMP%\staged.lst") DO IF %%~zS GTR 0 SET "HAS_STAGED=1"

IF /I "%ARG_FIRST%"=="--all" (
    git ls-files > "%HOOK_TMP%\work_files.lst"
) ELSE IF "%HAS_STAGED%"=="0" (
    git ls-files > "%HOOK_TMP%\work_files.lst"
) ELSE (
    COPY /Y "%HOOK_TMP%\staged.lst" "%HOOK_TMP%\work_files.lst" >NUL
)

FOR /F "usebackq delims=" %%F IN ("%HOOK_TMP%\work_files.lst") DO (
    CALL :NORMALIZE_ONE_FILE "%%F"
    IF ERRORLEVEL 1 GOTO :FAIL
)

REM ----------------------------------------------------------------------------
REM 2. Lint (Assembly Doc/CFG/ABI/Purity, Clippy, Rustdoc)
REM ----------------------------------------------------------------------------
ECHO [pre-commit] 2/6 Running static lints (asm-lint, clippy, rustdoc)...

cargo run --release --quiet -p asm-lint -- --dir asm > "%HOOK_TMP%\asm_lint.log"
IF ERRORLEVEL 1 (
    TYPE "%HOOK_TMP%\asm_lint.log" 1>&2
    GOTO :FAIL
)
TYPE "%HOOK_TMP%\asm_lint.log"

cargo clippy --workspace --all-targets --all-features --quiet -- -D warnings
IF ERRORLEVEL 1 GOTO :FAIL

SET "PREV_RUSTDOCFLAGS=%RUSTDOCFLAGS%"
SET "RUSTDOCFLAGS=-D warnings"
cargo doc --workspace --no-deps --quiet
IF ERRORLEVEL 1 (
    SET "RUSTDOCFLAGS=%PREV_RUSTDOCFLAGS%"
    GOTO :FAIL
)
SET "RUSTDOCFLAGS=%PREV_RUSTDOCFLAGS%"

REM ----------------------------------------------------------------------------
REM 3. Build (Multi-ISA, Multi-Assembler Lowering, Assembly & Verification)
REM ----------------------------------------------------------------------------
ECHO [pre-commit] 3/6 Building multi-ISA targets via BUILD.CMD...

CALL BUILD.CMD all
IF ERRORLEVEL 1 GOTO :FAIL

IF EXIST "build/nasm/dos16/WINCMD.COM" IF EXIST "build/fasm/dos16/WINCMD.COM" IF EXIST "build/jwasm/dos16/WINCMD.COM" (
    cargo run --release --quiet -p asm-lint -- --equiv WINCMD.COM build/nasm/dos16/WINCMD.COM build/fasm/dos16/WINCMD.COM build/jwasm/dos16/WINCMD.COM || GOTO :FAIL
    cargo run --release --quiet -p asm-lint -- --equiv ASMCONV.COM build/nasm/dos16/ASMCONV.COM build/fasm/dos16/ASMCONV.COM build/jwasm/dos16/ASMCONV.COM || GOTO :FAIL
    cargo run --release --quiet -p asm-lint -- --budget build/nasm/dos16/WINCMD.COM 49152 || GOTO :FAIL
    cargo run --release --quiet -p asm-lint -- --budget build/nasm/dos16/ASMCONV.COM 4096 || GOTO :FAIL
)
IF EXIST "build/nasm/win16/WINCMD16.EXE" IF EXIST "build/fasm/win16/WINCMD16.EXE" IF EXIST "build/jwasm/win16/WINCMD16.EXE" (
    cargo run --release --quiet -p asm-lint -- --equiv WINCMD.PIF build/nasm/win16/WINCMD.PIF build/fasm/win16/WINCMD.PIF build/jwasm/win16/WINCMD.PIF || GOTO :FAIL
    cargo run --release --quiet -p asm-lint -- --equiv WINCMD16.EXE build/nasm/win16/WINCMD16.EXE build/fasm/win16/WINCMD16.EXE build/jwasm/win16/WINCMD16.EXE || GOTO :FAIL
    cargo run --release --quiet -p asm-lint -- --equiv WINCMD286.EXE build/nasm/win16/WINCMD286.EXE build/fasm/win16/WINCMD286.EXE build/jwasm/win16/WINCMD286.EXE || GOTO :FAIL
)

REM ----------------------------------------------------------------------------
REM 4. Test (Rust Workspace Tests, 8086 Emulator Era Matrix, Fuzzing & Native)
REM ----------------------------------------------------------------------------
ECHO [pre-commit] 4/6 Running workspace unit/integration tests, emulator coverage ^& differential fuzzing...

cargo test --workspace --quiet
IF ERRORLEVEL 1 GOTO :FAIL

IF EXIST "build/nasm/dos16/WINCMD.COM" (
    FOR %%E IN (1.25 2.00 4.00 7.10) DO (
        cargo run --release --quiet -p emu-cov -- --com build/nasm/dos16/WINCMD.COM --era %%E --args " /C VER" || GOTO :FAIL
        IF EXIST "build/fasm/dos16/WINCMD.COM" IF EXIST "build/jwasm/dos16/WINCMD.COM" (
            cargo run --release --quiet -p diff-fuzz -- --era %%E --cases 500 build/nasm/dos16/WINCMD.COM build/fasm/dos16/WINCMD.COM build/jwasm/dos16/WINCMD.COM || GOTO :FAIL
        )
    )
)

IF EXIST "build/fasm/x86_64/WINCMD64.EXE" (
    "build/fasm/x86_64/WINCMD64.EXE" /C VER || GOTO :FAIL
)

REM ----------------------------------------------------------------------------
REM 5. Compute '% doc coverage' & '% test coverage'
REM ----------------------------------------------------------------------------
ECHO [pre-commit] 5/6 Measuring documentation coverage and test coverage...

SET "ASM_MOD_COUNT=0"
FOR /F "usebackq tokens=1,2" %%A IN ("%HOOK_TMP%\asm_lint.log") DO (
    IF "%%A"=="asm-lint:" SET "ASM_MOD_COUNT=%%B"
)

SET "DOC_PCT=100.00"
cargo +nightly --version >NUL 2>NUL
IF NOT ERRORLEVEL 1 (
    CALL :COMPUTE_NIGHTLY_DOC_COV
)

WHERE cargo-llvm-cov >NUL 2>NUL
IF ERRORLEVEL 1 (
    ECHO [pre-commit] ERROR: 'cargo-llvm-cov' is required in PATH to compute test coverage. 1>&2
    GOTO :FAIL
)
cargo llvm-cov --workspace --quiet --summary-only > "%HOOK_TMP%\llvm_cov.log"
IF ERRORLEVEL 1 GOTO :FAIL

SET "TEST_PCT="
FOR /F "usebackq tokens=1,10" %%A IN ("%HOOK_TMP%\llvm_cov.log") DO (
    IF "%%A"=="TOTAL" SET "TEST_PCT=%%B"
)
IF NOT DEFINED TEST_PCT (
    ECHO [pre-commit] ERROR: Failed to parse test coverage percentage from cargo llvm-cov. 1>&2
    GOTO :FAIL
)
SET "TEST_PCT=%TEST_PCT:%%=%"

CALL :COLOR_FOR_PCT "%DOC_PCT%" DOC_COLOR
CALL :COLOR_FOR_PCT "%TEST_PCT%" TEST_COLOR

SET "DOC_SHIELD=![doc coverage](https://img.shields.io/badge/doc%%20coverage-%DOC_PCT%%%25-%DOC_COLOR%)"
SET "TEST_SHIELD=![test coverage](https://img.shields.io/badge/test%%20coverage-%TEST_PCT%%%25-%TEST_COLOR%)"

REM ----------------------------------------------------------------------------
REM 6. Add / Update Coverage Shields in README.md
REM ----------------------------------------------------------------------------
ECHO [pre-commit] 6/6 Updating doc coverage (%DOC_PCT%%%) and test coverage (%TEST_PCT%%%) shields in README.md...

IF NOT EXIST "README.md" (
    ECHO [pre-commit] ERROR: README.md not found at repository root. 1>&2
    GOTO :FAIL
)

CALL :REWRITE_README "README.md" "%HOOK_TMP%\README_num.txt" "%HOOK_TMP%\README.md"
IF ERRORLEVEL 1 GOTO :FAIL

dos2unix -q -- "%HOOK_TMP%\README.md"

FC /B "%HOOK_TMP%\README.md" "README.md" >NUL 2>NUL
IF ERRORLEVEL 1 (
    COPY /Y "%HOOK_TMP%\README.md" "README.md" >NUL
    dos2unix -q -- "README.md"
    IF "%HAS_STAGED%"=="1" (
        git add -- README.md || GOTO :FAIL
    )
)

ECHO [pre-commit] All pre-commit checks passed (doc coverage: %DOC_PCT%%%, test coverage: %TEST_PCT%%%).
RMDIR /S /Q "%HOOK_TMP%" 2>NUL
POPD
ENDLOCAL
EXIT /B 0

:FAIL
IF EXIST "%HOOK_TMP%" RMDIR /S /Q "%HOOK_TMP%" 2>NUL
POPD
ENDLOCAL
EXIT /B 1

REM ============================================================================
REM Subroutines
REM ============================================================================

:NORMALIZE_ONE_FILE
SET "REL_FILE=%~1"
IF NOT DEFINED REL_FILE EXIT /B 0
FOR %%I IN ("%REL_FILE%") DO SET "CAND_FULL=%%~fI"
IF NOT EXIST "%CAND_FULL%" EXIT /B 0
IF EXIST "%CAND_FULL%\*" EXIT /B 0

REM Never rewrite this running batch script in-place while cmd.exe holds its offset.
IF /I "%CAND_FULL%"=="%SELF_PATH%" EXIT /B 0

CALL :CLASSIFY_EOL "%REL_FILE%" EOL_KIND
IF "%EOL_KIND%"=="binary" EXIT /B 0

SET "HAD_UNSTAGED=0"
git diff --quiet -- "%REL_FILE%" 2>NUL
IF ERRORLEVEL 1 SET "HAD_UNSTAGED=1"

COPY /Y "%CAND_FULL%" "%HOOK_TMP%\prev_file.bin" >NUL
IF "%EOL_KIND%"=="crlf" (
    unix2dos -q -- "%CAND_FULL%" || EXIT /B 1
) ELSE (
    dos2unix -q -- "%CAND_FULL%" || EXIT /B 1
)

FC /B "%HOOK_TMP%\prev_file.bin" "%CAND_FULL%" >NUL 2>NUL
IF ERRORLEVEL 1 (
    SET "IS_STAGED_FILE=0"
    FOR /F "usebackq delims=" %%S IN ("%HOOK_TMP%\staged.lst") DO (
        IF "%%S"=="%REL_FILE%" SET "IS_STAGED_FILE=1"
    )
    IF "%IS_STAGED_FILE%"=="1" (
        IF "%HAD_UNSTAGED%"=="0" (
            git add -- "%REL_FILE%" || EXIT /B 1
        ) ELSE (
            ECHO [pre-commit] ERROR: Normalized line endings in partially-staged file '%REL_FILE%'; review and git add it. 1>&2
            EXIT /B 1
        )
    )
)
EXIT /B 0

:CLASSIFY_EOL
SET "F_NAME=%~nx1"
SET "F_EXT=%~x1"
IF /I "%F_NAME%"=="AUTOEXEC.BAT" ( SET "%~2=crlf" & EXIT /B 0 )
IF /I "%F_NAME%"=="CONFIG.SYS"   ( SET "%~2=crlf" & EXIT /B 0 )
FOR %%X IN (.bin .com .exe .dll .so .dylib .sys .drv .vxd .386 .ovl .ovr .pif .scr .cpl .ocx .efi .rom .elf .o .obj .omf .a .lib .exp .res .pch .pdb .ilk .dbg .sym .msg .bsc .sbr .img .ima .flp .vfd .dsk .iso .vhd .vhdx .vmdk .vdi .qcow .qcow2 .dmg .ico .cur .bmp .png .jpg .jpeg .gif .webp .woff .woff2 .ttf .otf .fon .fnt .pdf .zip .gz .tgz .bz2 .xz .zst .7z .cab .msi .rar .tar .arj .lzh) DO (
    IF /I "%F_EXT%"=="%%X" ( SET "%~2=binary" & EXIT /B 0 )
)
FOR %%X IN (.bat .cmd .btm .reg .inf .ini .rc .dlg .mc .ps1 .psm1 .psd1 .vbs .wsf .sln .vcxproj .filters .props .targets) DO (
    IF /I "%F_EXT%"=="%%X" ( SET "%~2=crlf" & EXIT /B 0 )
)
SET "%~2=lf"
EXIT /B 0

:COMPUTE_NIGHTLY_DOC_COV
SET "PREV_RDF=%RUSTDOCFLAGS%"
SET "RUSTDOCFLAGS=-Z unstable-options --coverage"
cargo +nightly doc --workspace --no-deps > "%HOOK_TMP%\doc_cov.log" 2>&1
IF ERRORLEVEL 1 (
    SET "RUSTDOCFLAGS=%PREV_RDF%"
    EXIT /B 0
)
SET "RUSTDOCFLAGS=%PREV_RDF%"
SET /A "DOC_TOTAL=%ASM_MOD_COUNT%, DOC_WITH=%ASM_MOD_COUNT%"
FOR /F "usebackq tokens=1,2,3*" %%A IN ("%HOOK_TMP%\doc_cov.log") DO (
    IF "%%A"=="Generated" IF "%%B"=="output" IF "%%C"=="into" (
        IF EXIST "%%~D" CALL :ACCUM_RUSTDOC_TABLE "%%~D"
    )
)
IF %DOC_TOTAL% LEQ 0 EXIT /B 0
SET /A "DOC_X100=(DOC_WITH * 10000 + DOC_TOTAL / 2) / DOC_TOTAL"
SET /A "DOC_INT=DOC_X100 / 100, DOC_FRAC=DOC_X100 %% 100"
IF %DOC_FRAC% LSS 10 (
    SET "DOC_PCT=%DOC_INT%.0%DOC_FRAC%"
) ELSE (
    SET "DOC_PCT=%DOC_INT%.%DOC_FRAC%"
)
EXIT /B 0

:ACCUM_RUSTDOC_TABLE
FOR /F "usebackq tokens=1,2,3 delims=| " %%A IN ("%~1") DO (
    IF "%%A"=="Total" CALL :ACCUM_ROW "%%B" "%%C"
)
EXIT /B 0

:ACCUM_ROW
SET "R_DOC=%~1"
SET "R_PCT=%~2"
SET "R_PCT=%R_PCT:%%=%"
FOR /F "tokens=1,2 delims=." %%I IN ("%R_PCT%") DO (
    SET /A "P_TENTHS=%%I * 10 + %%J"
)
IF %R_DOC% GTR 0 IF %P_TENTHS% GTR 0 (
    SET /A "C_TOT=(R_DOC * 1000 + P_TENTHS / 2) / P_TENTHS"
    SET /A "DOC_WITH+=R_DOC, DOC_TOTAL+=C_TOT"
)
EXIT /B 0

:COLOR_FOR_PCT
FOR /F "tokens=1 delims=." %%I IN ("%~1") DO SET /A "PCT_WHOLE=%%I"
IF %PCT_WHOLE% GEQ 90 ( SET "%~2=brightgreen" & EXIT /B 0 )
IF %PCT_WHOLE% GEQ 80 ( SET "%~2=green"       & EXIT /B 0 )
IF %PCT_WHOLE% GEQ 70 ( SET "%~2=yellowgreen" & EXIT /B 0 )
IF %PCT_WHOLE% GEQ 60 ( SET "%~2=yellow"      & EXIT /B 0 )
IF %PCT_WHOLE% GEQ 50 ( SET "%~2=orange"      & EXIT /B 0 )
SET "%~2=red"
EXIT /B 0

:REWRITE_README
SET "IN_README=%~1"
SET "NUM_README=%~2"
SET "OUT_README=%~3"
SET "HAS_DOC=0"
SET "HAS_TEST=0"
FOR /F "usebackq delims=" %%L IN ("%IN_README%") DO (
    SET "SCAN_LINE=%%L"
    CALL :SCAN_README_FLAGS
)
git --no-pager grep --no-index -n -h --no-color "" -- "%IN_README%" > "%NUM_README%"
IF ERRORLEVEL 2 EXIT /B 1
SET "INSERTED_NEW=0"
SET "NEED_BLANK_AFTER=0"
TYPE NUL > "%OUT_README%"
FOR /F "usebackq delims=" %%L IN ("%NUM_README%") DO (
    SET "CUR_LINE=%%L"
    CALL :EMIT_README_LINE >> "%OUT_README%"
)
EXIT /B 0

:SCAN_README_FLAGS
SETLOCAL ENABLEDELAYEDEXPANSION
SET "S=!SCAN_LINE!"
SET "D=0"
SET "T=0"
IF "!S:~1,15!"=="[doc coverage](" SET "D=1"
IF "!S:~1,16!"=="[test coverage](" SET "T=1"
IF "!D!"=="1" (
    ENDLOCAL & SET "HAS_DOC=1"
    EXIT /B 0
)
IF "!T!"=="1" (
    ENDLOCAL & SET "HAS_TEST=1"
    EXIT /B 0
)
ENDLOCAL
EXIT /B 0

:EMIT_README_LINE
SETLOCAL ENABLEDELAYEDEXPANSION
SET "TXT=!CUR_LINE:*:=!"
IF "!NEED_BLANK_AFTER!"=="1" (
    IF NOT "!TXT!"=="" ECHO(
    ENDLOCAL & SET "NEED_BLANK_AFTER=0"
    GOTO :EMIT_README_LINE_BODY
)
ENDLOCAL

:EMIT_README_LINE_BODY
SETLOCAL ENABLEDELAYEDEXPANSION
SET "TXT=!CUR_LINE:*:=!"
IF "!TXT:~1,15!"=="[doc coverage](" (
    ECHO(!DOC_SHIELD!
    IF "!HAS_TEST!"=="0" ECHO(!TEST_SHIELD!
    ENDLOCAL
    EXIT /B 0
)
IF "!TXT:~1,16!"=="[test coverage](" (
    IF "!HAS_DOC!"=="0" ECHO(!DOC_SHIELD!
    ECHO(!TEST_SHIELD!
    ENDLOCAL
    EXIT /B 0
)
ECHO(!TXT!
IF "!HAS_DOC!"=="0" IF "!HAS_TEST!"=="0" IF "!INSERTED_NEW!"=="0" (
    IF "!TXT:~0,3!"=="===" (
        ECHO(
        ECHO(!DOC_SHIELD!
        ECHO(!TEST_SHIELD!
        ENDLOCAL & SET "INSERTED_NEW=1" & SET "NEED_BLANK_AFTER=1"
        EXIT /B 0
    )
    IF "!TXT:~0,2!"=="# " (
        ECHO(
        ECHO(!DOC_SHIELD!
        ECHO(!TEST_SHIELD!
        ENDLOCAL & SET "INSERTED_NEW=1" & SET "NEED_BLANK_AFTER=1"
        EXIT /B 0
    )
)
ENDLOCAL
EXIT /B 0
