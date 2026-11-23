# Cross-ISA Batch & Companion Utility Regression Scripts (`tests/`)

This directory contains portable, FAT-8.3-safe `.BAT` and `.CMD` test suites exercised across:

- `SMOKE.BAT` — Cross-era MS-DOS (`1.25` / `2.00` / `4.00` / `7.10`) & Windows NT–11 shell smoke test (`VER`, `SET /A`, `IF`, `FOR`, `CALL :label`, redirection).
- `UTILS.BAT` — Standalone 19-utility companion suite verification script.
- `SELFHOST.CMD` — Windows NT / 10 / 11 self-hosting verification wrapper executed directly inside `WINCMD32.EXE`, `WINCMD64.EXE`, and `WINCMD_ARM64.EXE`.
