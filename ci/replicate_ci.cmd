@ECHO OFF
SETLOCAL
REM @file ci/replicate_ci.cmd
REM @brief Native Windows local CI runner for win-cmd-asm (inspired by SamuelMarks/c-ci).
REM Usage: ci\replicate_ci.cmd

ECHO === [win-ci] Step 1/5: Static assembly & Rust lints ===
cargo run --release --quiet -p asm-lint -- --dir asm || EXIT /B 1
cargo clippy --workspace --all-targets --all-features -- -D warnings || EXIT /B 1
SET RUSTDOCFLAGS=-D warnings
cargo doc --workspace --no-deps --quiet || EXIT /B 1

ECHO === [win-ci] Step 2/5: Lower & assemble via BUILD.CMD ===
CALL BUILD.CMD || EXIT /B 1

ECHO === [win-ci] Step 3/5: Rust workspace unit & integration tests ===
cargo test --workspace --quiet || EXIT /B 1

ECHO === [win-ci] Step 4/5: 4-era 8086/286 emulator verification ===
IF EXIST build\nasm\dos16\WINCMD.COM (
    FOR %%E IN (1.25 2.00 4.00 7.10) DO (
        cargo run --release --quiet -p emu-cov -- --com build\nasm\dos16\WINCMD.COM --era %%E --args " /C VER" || EXIT /B 1
    )
)

ECHO === [win-ci] Step 5/5: Native Windows kernel smoke tests ===
FOR %%B IN (build\nasm\x86_32\WINCMD32.EXE build\fasm\x86_32\WINCMD32.EXE build\jwasm\x86_32\WINCMD32.EXE build\nasm\x86_64\WINCMD64.EXE build\fasm\x86_64\WINCMD64.EXE build\jwasm\x86_64\WINCMD64.EXE) DO (
    IF EXIST "%%B" (
        ECHO [native-win] Running %%B /C VER
        "%%B" /C VER || EXIT /B 1
    )
)

ECHO === [win-ci] Completed successfully ===
ENDLOCAL
EXIT /B 0
