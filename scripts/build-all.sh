#!/usr/bin/env bash
# Multi-ISA, Multi-Assembler Build & Verification Engine called by Makefile.
set -euo pipefail

MODE="${1:-all}"
TOOLCHAIN="${TOOLCHAIN:-all}"
TARGET="${TARGET:-all}"
BUILD_DIR="${BUILD_DIR:-build}"
DIST_DIR="${DIST_DIR:-dist/src}"

has_tc() {
  local t="$1"
  [ "$TOOLCHAIN" = "all" ] && return 0
  case ",${TOOLCHAIN}," in
    *,"${t}",*) return 0 ;;
    *) return 1 ;;
  esac
}

has_target() {
  local tgt="$1"
  [ "$TARGET" = "all" ] && return 0
  case ",${TARGET}," in
    *,"${tgt}",*) return 0 ;;
    *) return 1 ;;
  esac
}

if [ -d /opt/homebrew/opt/llvm/bin ]; then
  LLVM_BIN="/opt/homebrew/opt/llvm/bin"
else
  LLVM_BIN="$(dirname "$(command -v llvm-mc 2>/dev/null || printf '/usr/bin/llvm-mc')")"
fi

TC_BIN="$(TOOLCHAIN="$TOOLCHAIN" ./scripts/bootstrap-toolchains.sh)"
export PATH="$TC_BIN:$LLVM_BIN:$PATH"

NASM_WARN=(-Ox -w+all -w-reloc -w+error)
JWASM_WARN=(-q -WX -W4)

run_lint() {
  cargo run --release --quiet -p asm-lint -- --dir asm
}

run_lower() {
  mkdir -p "$DIST_DIR"
  cargo run --release --quiet -p asm-conv -- --in-dir asm --out-dir "$DIST_DIR"
}

build_libs() {
  mkdir -p "$BUILD_DIR/libs"
  if command -v llvm-dlltool >/dev/null 2>&1; then
    if has_target x86_32 || has_target x86_64; then
      llvm-dlltool -m i386 -k -d asm/tc/arm_ms/k32_x86.def -l "$BUILD_DIR/libs/kernel32_x86.lib"
      llvm-dlltool -m i386:x86-64 -d asm/tc/arm_ms/k32_x64.def -l "$BUILD_DIR/libs/kernel32_x64.lib"
    fi
    if has_target arm32; then
      llvm-dlltool -m arm -d asm/tc/arm_ms/k32_arm.def -l "$BUILD_DIR/libs/kernel32_arm32.lib"
    fi
    if has_target arm64; then
      llvm-dlltool -m arm64 -d asm/tc/arm_ms/k32_arm.def -l "$BUILD_DIR/libs/kernel32_arm64.lib"
    fi
  fi
}

DOS16_UTILS=(
  mem:MEM.COM
  attrib:ATTRIB.COM
  find:FIND.COM
  findstr:FINDSTR.COM
  sort:SORT.COM
  more:MORE.COM
  fc:FC.COM
  comp:COMP.COM
  tree:TREE.COM
  where:WHERE.COM
  xcopy:XCOPY.COM
  replace:REPLACE.COM
  robocopy:ROBOCOPY.COM
  choice:CHOICE.COM
  timeout:TIMEOUT.COM
  truename:TRUENAME.COM
  reg:REG.COM
  hostname:HOSTNAME.COM
  whoami:WHOAMI.COM
)

build_dos16() {
  mkdir -p "$BUILD_DIR/nasm/dos16" "$BUILD_DIR/fasm/dos16" "$BUILD_DIR/jwasm/dos16"
  if has_tc nasm; then
    nasm "${NASM_WARN[@]}" -I "$DIST_DIR/nasm/dos16/" -f bin "$DIST_DIR/nasm/dos16/wincmd.asm" -l "$BUILD_DIR/nasm/dos16/WINCMD.LST" -o "$BUILD_DIR/nasm/dos16/WINCMD.COM"
    nasm "${NASM_WARN[@]}" -I "$DIST_DIR/nasm/dos16/" -f bin "$DIST_DIR/nasm/dos16/wincmd1.asm" -o "$BUILD_DIR/nasm/dos16/WINCMD1.COM"
    nasm "${NASM_WARN[@]}" -f bin "$DIST_DIR/nasm/dos16/wincmd_m.asm" -o "$BUILD_DIR/nasm/dos16/WINCMD.MSG"
    nasm "${NASM_WARN[@]}" -f bin "$DIST_DIR/nasm/dos16/util/asmconv.asm" -l "$BUILD_DIR/nasm/dos16/ASMCONV.LST" -o "$BUILD_DIR/nasm/dos16/ASMCONV.COM"
    for pair in "${DOS16_UTILS[@]}"; do
      u_src="${pair%%:*}"
      u_out="${pair##*:}"
      nasm "${NASM_WARN[@]}" -I "$DIST_DIR/nasm/dos16/util/" -f bin "$DIST_DIR/nasm/dos16/util/${u_src}.asm" -o "$BUILD_DIR/nasm/dos16/${u_out}"
    done
  fi
  if has_tc fasm; then
    fasm "$DIST_DIR/fasm/dos16/wincmd.asm" "$BUILD_DIR/fasm/dos16/WINCMD.COM" >/dev/null
    fasm "$DIST_DIR/fasm/dos16/wincmd1.asm" "$BUILD_DIR/fasm/dos16/WINCMD1.COM" >/dev/null
    fasm "$DIST_DIR/fasm/dos16/wincmd_m.asm" "$BUILD_DIR/fasm/dos16/WINCMD.MSG" >/dev/null
    fasm "$DIST_DIR/fasm/dos16/util/asmconv.asm" "$BUILD_DIR/fasm/dos16/ASMCONV.COM" >/dev/null
    for pair in "${DOS16_UTILS[@]}"; do
      u_src="${pair%%:*}"
      u_out="${pair##*:}"
      fasm "$DIST_DIR/fasm/dos16/util/${u_src}.asm" "$BUILD_DIR/fasm/dos16/${u_out}" >/dev/null
    done
  fi
  if has_tc jwasm; then
    jwasm "${JWASM_WARN[@]}" -I"$DIST_DIR/jwasm/dos16" -bin -Fl="$BUILD_DIR/jwasm/dos16/WINCMD.LST" -Fo"$BUILD_DIR/jwasm/dos16/WINCMD.COM" "$DIST_DIR/jwasm/dos16/wincmd.asm"
    jwasm "${JWASM_WARN[@]}" -I"$DIST_DIR/jwasm/dos16" -bin -Fo"$BUILD_DIR/jwasm/dos16/WINCMD1.COM" "$DIST_DIR/jwasm/dos16/wincmd1.asm"
    jwasm "${JWASM_WARN[@]}" -bin -Fo"$BUILD_DIR/jwasm/dos16/WINCMD.MSG" "$DIST_DIR/jwasm/dos16/wincmd_m.asm"
    jwasm "${JWASM_WARN[@]}" -bin -Fl="$BUILD_DIR/jwasm/dos16/ASMCONV.LST" -Fo"$BUILD_DIR/jwasm/dos16/ASMCONV.COM" "$DIST_DIR/jwasm/dos16/util/asmconv.asm"
    for pair in "${DOS16_UTILS[@]}"; do
      u_src="${pair%%:*}"
      u_out="${pair##*:}"
      jwasm "${JWASM_WARN[@]}" -I"$DIST_DIR/jwasm/dos16/util" -bin -Fo"$BUILD_DIR/jwasm/dos16/${u_out}" "$DIST_DIR/jwasm/dos16/util/${u_src}.asm"
    done
  fi
}

build_win16() {
  mkdir -p "$BUILD_DIR/nasm/win16" "$BUILD_DIR/fasm/win16" "$BUILD_DIR/jwasm/win16"
  for pair in wincmd_pif:WINCMD.PIF w16_main:WINCMD16.EXE w16_286:WINCMD286.EXE; do
    src="${pair%%:*}"
    out="${pair##*:}"
    if has_tc nasm; then
      nasm "${NASM_WARN[@]}" -I "$DIST_DIR/nasm/win16/" -f bin "$DIST_DIR/nasm/win16/${src}.asm" -o "$BUILD_DIR/nasm/win16/${out}"
    fi
    if has_tc fasm; then
      fasm "$DIST_DIR/fasm/win16/${src}.asm" "$BUILD_DIR/fasm/win16/${out}" >/dev/null
    fi
    if has_tc jwasm; then
      jwasm "${JWASM_WARN[@]}" -I"$DIST_DIR/jwasm/win16" -bin -Fo"$BUILD_DIR/jwasm/win16/${out}" "$DIST_DIR/jwasm/win16/${src}.asm"
    fi
  done
}

build_x86_32() {
  mkdir -p "$BUILD_DIR/nasm/x86_32" "$BUILD_DIR/fasm/x86_32" "$BUILD_DIR/jwasm/x86_32"
  if has_tc nasm; then
    nasm "${NASM_WARN[@]}" -I "$DIST_DIR/nasm/x86_32/" -f win32 "$DIST_DIR/nasm/x86_32/wincmd32.asm" -l "$BUILD_DIR/nasm/x86_32/WINCMD32.LST" -o "$BUILD_DIR/nasm/x86_32/wincmd32.obj"
    lld-link /NOLOGO /NODEFAULTLIB /BREPRO /SUBSYSTEM:CONSOLE,3.10 /SAFESEH /DYNAMICBASE /NXCOMPAT /ENTRY:mainCRTStartup "/MAP:$BUILD_DIR/nasm/x86_32/WINCMD32.MAP" "$BUILD_DIR/nasm/x86_32/wincmd32.obj" "$BUILD_DIR/libs/kernel32_x86.lib" "/OUT:$BUILD_DIR/nasm/x86_32/WINCMD32.EXE"
  fi
  if has_tc fasm; then
    fasm "$DIST_DIR/fasm/x86_32/wincmd32.asm" "$BUILD_DIR/fasm/x86_32/wincmd32.obj" >/dev/null
    lld-link /NOLOGO /NODEFAULTLIB /BREPRO /SUBSYSTEM:CONSOLE,3.10 /SAFESEH /DYNAMICBASE /NXCOMPAT /ENTRY:mainCRTStartup "/MAP:$BUILD_DIR/fasm/x86_32/WINCMD32.MAP" "$BUILD_DIR/fasm/x86_32/wincmd32.obj" "$BUILD_DIR/libs/kernel32_x86.lib" "/OUT:$BUILD_DIR/fasm/x86_32/WINCMD32.EXE"
  fi
  if has_tc jwasm; then
    jwasm "${JWASM_WARN[@]}" -I"$DIST_DIR/jwasm/x86_32" -coff -safeseh -Fl="$BUILD_DIR/jwasm/x86_32/WINCMD32.LST" -Fo"$BUILD_DIR/jwasm/x86_32/wincmd32.obj" "$DIST_DIR/jwasm/x86_32/wincmd32.asm"
    lld-link /NOLOGO /NODEFAULTLIB /BREPRO /SUBSYSTEM:CONSOLE,3.10 /SAFESEH /DYNAMICBASE /NXCOMPAT /ENTRY:mainCRTStartup "/MAP:$BUILD_DIR/jwasm/x86_32/WINCMD32.MAP" "$BUILD_DIR/jwasm/x86_32/wincmd32.obj" "$BUILD_DIR/libs/kernel32_x86.lib" "/OUT:$BUILD_DIR/jwasm/x86_32/WINCMD32.EXE"
  fi
}

build_x86_64() {
  mkdir -p "$BUILD_DIR/nasm/x86_64" "$BUILD_DIR/fasm/x86_64" "$BUILD_DIR/jwasm/x86_64"
  if has_tc nasm; then
    nasm "${NASM_WARN[@]}" -I "$DIST_DIR/nasm/x86_64/" -f win64 "$DIST_DIR/nasm/x86_64/wincmd64.asm" -l "$BUILD_DIR/nasm/x86_64/WINCMD64.LST" -o "$BUILD_DIR/nasm/x86_64/wincmd64.obj"
    lld-link /NOLOGO /NODEFAULTLIB /BREPRO /STACK:1048576,65536 /SUBSYSTEM:CONSOLE,5.02 /HIGHENTROPYVA /DYNAMICBASE /NXCOMPAT /CETCOMPAT /GUARD:CF /MANIFEST:EMBED /MANIFESTINPUT:asm/x86_64/wincmd64.manifest /ENTRY:mainCRTStartup "/MAP:$BUILD_DIR/nasm/x86_64/WINCMD64.MAP" "$BUILD_DIR/nasm/x86_64/wincmd64.obj" "$BUILD_DIR/libs/kernel32_x64.lib" "/OUT:$BUILD_DIR/nasm/x86_64/WINCMD64.EXE"
  fi
  if has_tc fasm; then
    fasm "$DIST_DIR/fasm/x86_64/wincmd64.asm" "$BUILD_DIR/fasm/x86_64/WINCMD64.EXE" >/dev/null
  fi
  if has_tc jwasm; then
    jwasm "${JWASM_WARN[@]}" -I"$DIST_DIR/jwasm/x86_64" -win64 -Fl="$BUILD_DIR/jwasm/x86_64/WINCMD64.LST" -Fo"$BUILD_DIR/jwasm/x86_64/wincmd64.obj" "$DIST_DIR/jwasm/x86_64/wincmd64.asm"
    lld-link /NOLOGO /NODEFAULTLIB /BREPRO /STACK:1048576,65536 /SUBSYSTEM:CONSOLE,5.02 /HIGHENTROPYVA /DYNAMICBASE /NXCOMPAT /CETCOMPAT /GUARD:CF /MANIFEST:EMBED /MANIFESTINPUT:asm/x86_64/wincmd64.manifest /ENTRY:mainCRTStartup "/MAP:$BUILD_DIR/jwasm/x86_64/WINCMD64.MAP" "$BUILD_DIR/jwasm/x86_64/wincmd64.obj" "$BUILD_DIR/libs/kernel32_x64.lib" "/OUT:$BUILD_DIR/jwasm/x86_64/WINCMD64.EXE"
  fi
}

build_arm32() {
  mkdir -p "$BUILD_DIR/fasmarm/arm32" "$BUILD_DIR/llvm/arm32"
  if has_tc fasmarm; then
    fasmarm "$DIST_DIR/fasmarm/arm32/v4/wincmdv4.asm" "$BUILD_DIR/fasmarm/arm32/WINCMD_ARMV4.EXE" >/dev/null
    fasmarm "$DIST_DIR/fasmarm/arm32/v7/wincmdv7.asm" "$BUILD_DIR/fasmarm/arm32/WINCMD_ARM32.EXE" >/dev/null
  fi
  if has_tc llvm; then
    llvm-mc -triple=armv4t-none-eabi --fatal-warnings -I "$DIST_DIR/llvm-mc/arm32/v4" -filetype=obj "$DIST_DIR/llvm-mc/arm32/v4/wincmdv4.asm" -o "$BUILD_DIR/llvm/arm32/wincmdv4.obj"
    llvm-mc -triple=thumbv7a-pc-windows-msvc -mattr=+hwdiv --fatal-warnings -I "$DIST_DIR/llvm-mc/arm32/v7" -filetype=obj "$DIST_DIR/llvm-mc/arm32/v7/wincmdv7.asm" -o "$BUILD_DIR/llvm/arm32/wincmdv7.obj"
    lld-link /WX /NOLOGO /NODEFAULTLIB /BREPRO /STACK:1048576,65536 /SUBSYSTEM:CONSOLE,6.02 /DYNAMICBASE /NXCOMPAT /GUARD:CF /MANIFEST:EMBED /MANIFESTINPUT:asm/x86_64/wincmd64.manifest /ENTRY:mainCRTStartup "/MAP:$BUILD_DIR/llvm/arm32/WINCMD_ARM32.MAP" "$BUILD_DIR/llvm/arm32/wincmdv7.obj" "$BUILD_DIR/libs/kernel32_arm32.lib" "/OUT:$BUILD_DIR/llvm/arm32/WINCMD_ARM32.EXE"
  fi
}

build_arm64() {
  mkdir -p "$BUILD_DIR/fasmarm/arm64" "$BUILD_DIR/llvm/arm64"
  if has_tc fasmarm; then
    fasmarm "$DIST_DIR/fasmarm/arm64/wincmda64.asm" "$BUILD_DIR/fasmarm/arm64/WINCMD_ARM64.EXE" >/dev/null
  fi
  if has_tc llvm; then
    llvm-mc -triple=aarch64-pc-windows-msvc --fatal-warnings -I "$DIST_DIR/llvm-mc/arm64" -filetype=obj "$DIST_DIR/llvm-mc/arm64/wincmda64.asm" -o "$BUILD_DIR/llvm/arm64/wincmda64.obj"
    lld-link /WX /NOLOGO /NODEFAULTLIB /BREPRO /STACK:1048576,65536 /SUBSYSTEM:CONSOLE,10.0 /HIGHENTROPYVA /DYNAMICBASE /NXCOMPAT /GUARD:CF /MANIFEST:EMBED /MANIFESTINPUT:asm/x86_64/wincmd64.manifest /ENTRY:mainCRTStartup "/MAP:$BUILD_DIR/llvm/arm64/WINCMD_ARM64.MAP" "$BUILD_DIR/llvm/arm64/wincmda64.obj" "$BUILD_DIR/libs/kernel32_arm64.lib" "/OUT:$BUILD_DIR/llvm/arm64/WINCMD_ARM64.EXE"
  fi
}

verify_equiv_group() {
  local label="$1"
  local subdir="$2"
  local fname="$3"
  local files=()
  for tc in nasm fasm jwasm; do
    if has_tc "$tc" && [ -f "$BUILD_DIR/$tc/$subdir/$fname" ]; then
      files+=("$BUILD_DIR/$tc/$subdir/$fname")
    fi
  done
  if [ "${#files[@]}" -ge 2 ]; then
    cargo run --release --quiet -p asm-lint -- --equiv "$label" "${files[@]}"
  fi
}

verify_all() {
  if has_target dos16; then
    verify_equiv_group WINCMD.COM dos16 WINCMD.COM
    verify_equiv_group WINCMD1.COM dos16 WINCMD1.COM
    verify_equiv_group WINCMD.MSG dos16 WINCMD.MSG
    verify_equiv_group ASMCONV.COM dos16 ASMCONV.COM
    for pair in "${DOS16_UTILS[@]}"; do
      u_out="${pair##*:}"
      verify_equiv_group "$u_out" dos16 "$u_out"
    done
    for tc in nasm fasm jwasm; do
      if has_tc "$tc" && [ -f "$BUILD_DIR/$tc/dos16/WINCMD.COM" ]; then
        cargo run --release --quiet -p asm-lint -- --budget "$BUILD_DIR/$tc/dos16/WINCMD.COM" 49152
        cargo run --release --quiet -p asm-lint -- --budget "$BUILD_DIR/$tc/dos16/WINCMD1.COM" 20480
        cargo run --release --quiet -p asm-lint -- --budget "$BUILD_DIR/$tc/dos16/WINCMD.MSG" 4096
        cargo run --release --quiet -p asm-lint -- --budget "$BUILD_DIR/$tc/dos16/ASMCONV.COM" 4096
        util_sum=0
        for pair in "${DOS16_UTILS[@]}"; do
          u_out="${pair##*:}"
          cargo run --release --quiet -p asm-lint -- --budget "$BUILD_DIR/$tc/dos16/${u_out}" 12288
          sz="$(wc -c < "$BUILD_DIR/$tc/dos16/${u_out}" | tr -d ' ')"
          util_sum=$((util_sum + sz))
        done
        if [ "$util_sum" -gt 77824 ]; then
          echo "asm-lint error: $tc 19-utility .COM total ${util_sum} bytes > 77824 byte (76 KiB) budget" >&2
          exit 1
        fi
        wincmd_sz="$(wc -c < "$BUILD_DIR/$tc/dos16/WINCMD.COM" | tr -d ' ')"
        floppy_sum=$((wincmd_sz + util_sum))
        if [ "$floppy_sum" -gt 163840 ]; then
          echo "asm-lint error: $tc WINCMD.COM + 19 utilities ${floppy_sum} bytes > 163840 byte (160 KiB floppy) budget" >&2
          exit 1
        fi
        echo "asm-lint: \`$tc/dos16\` 19-utility suite ${util_sum} bytes <= 77824 B; floppy total ${floppy_sum} bytes <= 163840 B"
      fi
    done
  fi
  if has_target win16; then
    verify_equiv_group WINCMD.PIF win16 WINCMD.PIF
    verify_equiv_group WINCMD16.EXE win16 WINCMD16.EXE
    verify_equiv_group WINCMD286.EXE win16 WINCMD286.EXE
    for tc in nasm fasm jwasm; do
      if has_tc "$tc" && [ -f "$BUILD_DIR/$tc/win16/WINCMD.PIF" ]; then
        cargo run --release --quiet -p asm-lint -- --purity 8086 "$BUILD_DIR/$tc/win16/WINCMD16.EXE"
        cargo run --release --quiet -p asm-lint -- --purity 286 "$BUILD_DIR/$tc/win16/WINCMD286.EXE"
        cargo run --release --quiet -p asm-lint -- --budget "$BUILD_DIR/$tc/win16/WINCMD.PIF" 545
        cargo run --release --quiet -p asm-lint -- --budget "$BUILD_DIR/$tc/win16/WINCMD16.EXE" 65536
        cargo run --release --quiet -p asm-lint -- --budget "$BUILD_DIR/$tc/win16/WINCMD286.EXE" 65536
      fi
    done
  fi
  if has_target x86_32; then
    verify_equiv_group WINCMD32.EXE x86_32 WINCMD32.EXE
    for tc in nasm fasm jwasm; do
      if has_tc "$tc" && [ -f "$BUILD_DIR/$tc/x86_32/WINCMD32.EXE" ]; then
        cargo run --release --quiet -p asm-lint -- --purity 386 "$BUILD_DIR/$tc/x86_32/WINCMD32.EXE"
        cargo run --release --quiet -p asm-lint -- --budget "$BUILD_DIR/$tc/x86_32/WINCMD32.EXE" 131072
      fi
    done
  fi
  if has_target x86_64; then
    verify_equiv_group WINCMD64.EXE x86_64 WINCMD64.EXE
    for tc in nasm fasm jwasm; do
      if has_tc "$tc" && [ -f "$BUILD_DIR/$tc/x86_64/WINCMD64.EXE" ]; then
        cargo run --release --quiet -p asm-lint -- --purity x64 "$BUILD_DIR/$tc/x86_64/WINCMD64.EXE"
        cargo run --release --quiet -p asm-lint -- --budget "$BUILD_DIR/$tc/x86_64/WINCMD64.EXE" 131072
      fi
    done
  fi
  if has_target arm32; then
    if has_tc fasmarm && [ -f "$BUILD_DIR/fasmarm/arm32/WINCMD_ARMV4.EXE" ]; then
      cargo run --release --quiet -p asm-lint -- --purity armv4 "$BUILD_DIR/fasmarm/arm32/WINCMD_ARMV4.EXE"
      cargo run --release --quiet -p asm-lint -- --budget "$BUILD_DIR/fasmarm/arm32/WINCMD_ARMV4.EXE" 131072
    fi
    if has_tc fasmarm && [ -f "$BUILD_DIR/fasmarm/arm32/WINCMD_ARM32.EXE" ]; then
      cargo run --release --quiet -p asm-lint -- --purity armv7 "$BUILD_DIR/fasmarm/arm32/WINCMD_ARM32.EXE"
      cargo run --release --quiet -p asm-lint -- --budget "$BUILD_DIR/fasmarm/arm32/WINCMD_ARM32.EXE" 131072
    fi
    if has_tc llvm && [ -f "$BUILD_DIR/llvm/arm32/WINCMD_ARM32.EXE" ]; then
      cargo run --release --quiet -p asm-lint -- --purity armv7 "$BUILD_DIR/llvm/arm32/WINCMD_ARM32.EXE"
      cargo run --release --quiet -p asm-lint -- --budget "$BUILD_DIR/llvm/arm32/WINCMD_ARM32.EXE" 131072
    fi
  fi
  if has_target arm64; then
    if has_tc fasmarm && [ -f "$BUILD_DIR/fasmarm/arm64/WINCMD_ARM64.EXE" ]; then
      cargo run --release --quiet -p asm-lint -- --purity arm64 "$BUILD_DIR/fasmarm/arm64/WINCMD_ARM64.EXE"
      cargo run --release --quiet -p asm-lint -- --budget "$BUILD_DIR/fasmarm/arm64/WINCMD_ARM64.EXE" 131072
    fi
    if has_tc llvm && [ -f "$BUILD_DIR/llvm/arm64/WINCMD_ARM64.EXE" ]; then
      cargo run --release --quiet -p asm-lint -- --purity arm64 "$BUILD_DIR/llvm/arm64/WINCMD_ARM64.EXE"
      cargo run --release --quiet -p asm-lint -- --budget "$BUILD_DIR/llvm/arm64/WINCMD_ARM64.EXE" 131072
    fi
  fi
}

case "$MODE" in
  lint) run_lint ;;
  lower) run_lower ;;
  build)
    build_libs
    if has_target dos16; then build_dos16; fi
    if has_target win16; then build_win16; fi
    if has_target x86_32; then build_x86_32; fi
    if has_target x86_64; then build_x86_64; fi
    if has_target arm32; then build_arm32; fi
    if has_target arm64; then build_arm64; fi
    ;;
  verify) verify_all ;;
  all)
    run_lint
    run_lower
    "$0" build
    verify_all
    ;;
  test)
    "$0" all
    cargo test --workspace --quiet
    cargo clippy --workspace --all-targets -- -D warnings
    RUSTDOCFLAGS="-D warnings" cargo doc --workspace --no-deps --quiet
    for era in 1.25 2.00 4.00 7.10; do
      cargo run --release --quiet -p emu-cov -- --com "$BUILD_DIR/nasm/dos16/WINCMD.COM" --era "$era" --args " /C VER"
      cargo run --release --quiet -p diff-fuzz -- --era "$era" --cases 500 "$BUILD_DIR/nasm/dos16/WINCMD.COM" "$BUILD_DIR/fasm/dos16/WINCMD.COM" "$BUILD_DIR/jwasm/dos16/WINCMD.COM"
    done
    if command -v wine >/dev/null 2>&1; then
      WINEDEBUG=-all wine "$BUILD_DIR/nasm/x86_32/WINCMD32.EXE" /C VER
      WINEDEBUG=-all wine "$BUILD_DIR/fasm/x86_64/WINCMD64.EXE" /C VER
      WINEDEBUG=-all wine "$BUILD_DIR/jwasm/x86_64/WINCMD64.EXE" /C VER
    fi
    ;;
  fuzz)
    "$0" all
    for era in 1.25 2.00 4.00 7.10; do
      cargo run --release --quiet -p diff-fuzz -- --era "$era" --cases 2500 "$BUILD_DIR/nasm/dos16/WINCMD.COM" "$BUILD_DIR/fasm/dos16/WINCMD.COM" "$BUILD_DIR/jwasm/dos16/WINCMD.COM"
    done
    ;;
  *)
    echo "Unknown mode: $MODE" >&2
    exit 1
    ;;
esac
