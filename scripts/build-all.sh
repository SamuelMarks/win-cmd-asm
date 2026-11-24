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

EXTRA_LLVM_PATHS=""
for d in /opt/homebrew/opt/llvm/bin /opt/homebrew/opt/lld/bin /usr/local/opt/llvm/bin /usr/local/opt/lld/bin /usr/lib/llvm*/bin; do
  if [ -d "$d" ]; then
    EXTRA_LLVM_PATHS="${EXTRA_LLVM_PATHS:+$EXTRA_LLVM_PATHS:}$d"
  fi
done
if [ -z "$EXTRA_LLVM_PATHS" ]; then
  EXTRA_LLVM_PATHS="$(dirname "$(command -v llvm-mc 2>/dev/null || printf '/usr/bin/llvm-mc')")"
fi

if [ "$MODE" != "lint" ] && [ "$MODE" != "lower" ]; then
  TC_BIN="$(TOOLCHAIN="$TOOLCHAIN" ./scripts/bootstrap-toolchains.sh)"
  export PATH="$TC_BIN:$EXTRA_LLVM_PATHS:$PATH"
else
  export PATH="$EXTRA_LLVM_PATHS:$PATH"
fi

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

PE_UTILS=(
  mem:MEM.EXE
  attrib:ATTRIB.EXE
  find:FIND.EXE
  findstr:FINDSTR.EXE
  sort:SORT.EXE
  more:MORE.EXE
  fc:FC.EXE
  comp:COMP.EXE
  tree:TREE.EXE
  where:WHERE.EXE
  xcopy:XCOPY.EXE
  replace:REPLACE.EXE
  robocopy:ROBOCOPY.EXE
  choice:CHOICE.EXE
  timeout:TIMEOUT.EXE
  truename:TRUENAME.EXE
  reg:REG.EXE
  hostname:HOSTNAME.EXE
  whoami:WHOAMI.EXE
)

build_dos16() {
  if has_tc nasm; then
    mkdir -p "$BUILD_DIR/nasm/dos16"
    nasm "${NASM_WARN[@]}" -I "$DIST_DIR/nasm/dos16/" -f bin "$DIST_DIR/nasm/dos16/wincmd.asm" -l "$BUILD_DIR/nasm/dos16/WINCMD.LST" -o "$BUILD_DIR/nasm/dos16/WINCMD.COM"
    nasm "${NASM_WARN[@]}" -I "$DIST_DIR/nasm/dos16/" -f bin "$DIST_DIR/nasm/dos16/wincmd1.asm" -o "$BUILD_DIR/nasm/dos16/WINCMD1.COM"
    nasm "${NASM_WARN[@]}" -f bin "$DIST_DIR/nasm/dos16/wincmd_m.asm" -o "$BUILD_DIR/nasm/dos16/WINCMD.MSG"
    nasm "${NASM_WARN[@]}" -f bin "$DIST_DIR/nasm/dos16/util/asmconv.asm" -l "$BUILD_DIR/nasm/dos16/ASMCONV.LST" -o "$BUILD_DIR/nasm/dos16/ASMCONV.COM"
    nasm "${NASM_WARN[@]}" -f obj "$DIST_DIR/nasm/dos16/wincmdmz.asm" -o "$BUILD_DIR/nasm/dos16/wincmdmz.obj"
    if command -v wlink >/dev/null 2>&1; then
      wlink option quiet format dos file "$BUILD_DIR/nasm/dos16/wincmdmz.obj" name "$BUILD_DIR/nasm/dos16/WINCMDMZ.EXE"
    fi
    for pair in "${DOS16_UTILS[@]}"; do
      u_src="${pair%%:*}"
      u_out="${pair##*:}"
      nasm "${NASM_WARN[@]}" -I "$DIST_DIR/nasm/dos16/util/" -f bin "$DIST_DIR/nasm/dos16/util/${u_src}.asm" -o "$BUILD_DIR/nasm/dos16/${u_out}"
    done
  fi
  if has_tc fasm; then
    mkdir -p "$BUILD_DIR/fasm/dos16"
    fasm "$DIST_DIR/fasm/dos16/wincmd.asm" "$BUILD_DIR/fasm/dos16/WINCMD.COM" >/dev/null
    fasm "$DIST_DIR/fasm/dos16/wincmd1.asm" "$BUILD_DIR/fasm/dos16/WINCMD1.COM" >/dev/null
    fasm "$DIST_DIR/fasm/dos16/wincmd_m.asm" "$BUILD_DIR/fasm/dos16/WINCMD.MSG" >/dev/null
    fasm "$DIST_DIR/fasm/dos16/util/asmconv.asm" "$BUILD_DIR/fasm/dos16/ASMCONV.COM" >/dev/null
    fasm "$DIST_DIR/fasm/dos16/wincmdmz.asm" "$BUILD_DIR/fasm/dos16/WINCMDMZ.EXE" >/dev/null
    for pair in "${DOS16_UTILS[@]}"; do
      u_src="${pair%%:*}"
      u_out="${pair##*:}"
      fasm "$DIST_DIR/fasm/dos16/util/${u_src}.asm" "$BUILD_DIR/fasm/dos16/${u_out}" >/dev/null
    done
  fi
  if has_tc jwasm; then
    mkdir -p "$BUILD_DIR/jwasm/dos16"
    jwasm "${JWASM_WARN[@]}" -I"$DIST_DIR/jwasm/dos16" -bin -Fl="$BUILD_DIR/jwasm/dos16/WINCMD.LST" -Fo"$BUILD_DIR/jwasm/dos16/WINCMD.COM" "$DIST_DIR/jwasm/dos16/wincmd.asm"
    jwasm "${JWASM_WARN[@]}" -I"$DIST_DIR/jwasm/dos16" -bin -Fo"$BUILD_DIR/jwasm/dos16/WINCMD1.COM" "$DIST_DIR/jwasm/dos16/wincmd1.asm"
    jwasm "${JWASM_WARN[@]}" -bin -Fo"$BUILD_DIR/jwasm/dos16/WINCMD.MSG" "$DIST_DIR/jwasm/dos16/wincmd_m.asm"
    jwasm "${JWASM_WARN[@]}" -bin -Fl="$BUILD_DIR/jwasm/dos16/ASMCONV.LST" -Fo"$BUILD_DIR/jwasm/dos16/ASMCONV.COM" "$DIST_DIR/jwasm/dos16/util/asmconv.asm"
    jwasm "${JWASM_WARN[@]}" -omf -Fo"$BUILD_DIR/jwasm/dos16/wincmdmz.obj" "$DIST_DIR/jwasm/dos16/wincmdmz.asm"
    jwasm "${JWASM_WARN[@]}" -mz -Fo"$BUILD_DIR/jwasm/dos16/WINCMDMZ.EXE" "$DIST_DIR/jwasm/dos16/wincmdmz.asm"
    for pair in "${DOS16_UTILS[@]}"; do
      u_src="${pair%%:*}"
      u_out="${pair##*:}"
      jwasm "${JWASM_WARN[@]}" -I"$DIST_DIR/jwasm/dos16/util" -bin -Fo"$BUILD_DIR/jwasm/dos16/${u_out}" "$DIST_DIR/jwasm/dos16/util/${u_src}.asm"
    done
  fi
}

build_win16() {
  if has_tc nasm; then mkdir -p "$BUILD_DIR/nasm/win16"; fi
  if has_tc fasm; then mkdir -p "$BUILD_DIR/fasm/win16"; fi
  if has_tc jwasm; then mkdir -p "$BUILD_DIR/jwasm/win16"; fi
  for pair in w16_pif:WINCMD.PIF w16_main:WINCMD16.EXE w16_286:WINCMD286.EXE; do
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
  if has_tc nasm; then
    mkdir -p "$BUILD_DIR/nasm/x86_32"
    nasm "${NASM_WARN[@]}" -I "$DIST_DIR/nasm/x86_32/" -f win32 "$DIST_DIR/nasm/x86_32/wincmd32.asm" -l "$BUILD_DIR/nasm/x86_32/WINCMD32.LST" -o "$BUILD_DIR/nasm/x86_32/wincmd32.obj"
    lld-link /NOLOGO /NODEFAULTLIB /BREPRO /SUBSYSTEM:CONSOLE,3.10 /SAFESEH /DYNAMICBASE /NXCOMPAT /ENTRY:mainCRTStartup "/MAP:$BUILD_DIR/nasm/x86_32/WINCMD32.MAP" "$BUILD_DIR/nasm/x86_32/wincmd32.obj" "$BUILD_DIR/libs/kernel32_x86.lib" "/OUT:$BUILD_DIR/nasm/x86_32/WINCMD32.EXE"
    for pair in "${PE_UTILS[@]}"; do
      u_src="${pair%%:*}"
      u_out="${pair##*:}"
      nasm "${NASM_WARN[@]}" -I "$DIST_DIR/nasm/x86_32/util/" -f win32 "$DIST_DIR/nasm/x86_32/util/${u_src}.asm" -o "$BUILD_DIR/nasm/x86_32/u_${u_src}.obj"
      lld-link /NOLOGO /NODEFAULTLIB /BREPRO /SUBSYSTEM:CONSOLE,3.10 /SAFESEH /DYNAMICBASE /NXCOMPAT /ENTRY:mainCRTStartup "$BUILD_DIR/nasm/x86_32/u_${u_src}.obj" "$BUILD_DIR/libs/kernel32_x86.lib" "/OUT:$BUILD_DIR/nasm/x86_32/${u_out}"
    done
  fi
  if has_tc fasm; then
    mkdir -p "$BUILD_DIR/fasm/x86_32" "$BUILD_DIR/fasm-pe32/x86_32"
    fasm "$DIST_DIR/fasm/x86_32/wincmd32.asm" "$BUILD_DIR/fasm/x86_32/wincmd32.obj" >/dev/null
    lld-link /NOLOGO /NODEFAULTLIB /BREPRO /SUBSYSTEM:CONSOLE,3.10 /SAFESEH /DYNAMICBASE /NXCOMPAT /ENTRY:mainCRTStartup "/MAP:$BUILD_DIR/fasm/x86_32/WINCMD32.MAP" "$BUILD_DIR/fasm/x86_32/wincmd32.obj" "$BUILD_DIR/libs/kernel32_x86.lib" "/OUT:$BUILD_DIR/fasm/x86_32/WINCMD32.EXE"
    fasm "$DIST_DIR/fasm-pe32/x86_32/wincmd32.asm" "$BUILD_DIR/fasm-pe32/x86_32/WINCMD32.EXE" >/dev/null
    for pair in "${PE_UTILS[@]}"; do
      u_src="${pair%%:*}"
      u_out="${pair##*:}"
      fasm "$DIST_DIR/fasm/x86_32/util/${u_src}.asm" "$BUILD_DIR/fasm/x86_32/u_${u_src}.obj" >/dev/null
      lld-link /NOLOGO /NODEFAULTLIB /BREPRO /SUBSYSTEM:CONSOLE,3.10 /SAFESEH /DYNAMICBASE /NXCOMPAT /ENTRY:mainCRTStartup "$BUILD_DIR/fasm/x86_32/u_${u_src}.obj" "$BUILD_DIR/libs/kernel32_x86.lib" "/OUT:$BUILD_DIR/fasm/x86_32/${u_out}"
      fasm "$DIST_DIR/fasm-pe32/x86_32/util/${u_src}.asm" "$BUILD_DIR/fasm-pe32/x86_32/${u_out}" >/dev/null
    done
  fi
  if has_tc jwasm; then
    mkdir -p "$BUILD_DIR/jwasm/x86_32"
    jwasm "${JWASM_WARN[@]}" -I"$DIST_DIR/jwasm/x86_32" -coff -safeseh -Fl="$BUILD_DIR/jwasm/x86_32/WINCMD32.LST" -Fo"$BUILD_DIR/jwasm/x86_32/wincmd32.obj" "$DIST_DIR/jwasm/x86_32/wincmd32.asm"
    lld-link /NOLOGO /NODEFAULTLIB /BREPRO /SUBSYSTEM:CONSOLE,3.10 /SAFESEH /DYNAMICBASE /NXCOMPAT /ENTRY:mainCRTStartup "/MAP:$BUILD_DIR/jwasm/x86_32/WINCMD32.MAP" "$BUILD_DIR/jwasm/x86_32/wincmd32.obj" "$BUILD_DIR/libs/kernel32_x86.lib" "/OUT:$BUILD_DIR/jwasm/x86_32/WINCMD32.EXE"
    for pair in "${PE_UTILS[@]}"; do
      u_src="${pair%%:*}"
      u_out="${pair##*:}"
      jwasm "${JWASM_WARN[@]}" -I"$DIST_DIR/jwasm/x86_32/util" -coff -safeseh -Fo"$BUILD_DIR/jwasm/x86_32/u_${u_src}.obj" "$DIST_DIR/jwasm/x86_32/util/${u_src}.asm"
      lld-link /NOLOGO /NODEFAULTLIB /BREPRO /SUBSYSTEM:CONSOLE,3.10 /SAFESEH /DYNAMICBASE /NXCOMPAT /ENTRY:mainCRTStartup "$BUILD_DIR/jwasm/x86_32/u_${u_src}.obj" "$BUILD_DIR/libs/kernel32_x86.lib" "/OUT:$BUILD_DIR/jwasm/x86_32/${u_out}"
    done
  fi
}

build_x86_64() {
  if has_tc nasm; then
    mkdir -p "$BUILD_DIR/nasm/x86_64"
    nasm "${NASM_WARN[@]}" -I "$DIST_DIR/nasm/x86_64/" -f win64 "$DIST_DIR/nasm/x86_64/wincmd64.asm" -l "$BUILD_DIR/nasm/x86_64/WINCMD64.LST" -o "$BUILD_DIR/nasm/x86_64/wincmd64.obj"
    lld-link /NOLOGO /NODEFAULTLIB /BREPRO /STACK:1048576,65536 /SUBSYSTEM:CONSOLE,5.02 /HIGHENTROPYVA /DYNAMICBASE /NXCOMPAT /CETCOMPAT /GUARD:CF /MANIFEST:EMBED /MANIFESTINPUT:asm/x86_64/wincmd64.manifest /ENTRY:mainCRTStartup "/MAP:$BUILD_DIR/nasm/x86_64/WINCMD64.MAP" "$BUILD_DIR/nasm/x86_64/wincmd64.obj" "$BUILD_DIR/libs/kernel32_x64.lib" "/OUT:$BUILD_DIR/nasm/x86_64/WINCMD64.EXE"
    for pair in "${PE_UTILS[@]}"; do
      u_src="${pair%%:*}"
      u_out="${pair##*:}"
      nasm "${NASM_WARN[@]}" -I "$DIST_DIR/nasm/x86_64/util/" -f win64 "$DIST_DIR/nasm/x86_64/util/${u_src}.asm" -o "$BUILD_DIR/nasm/x86_64/u_${u_src}.obj"
      lld-link /NOLOGO /NODEFAULTLIB /BREPRO /STACK:1048576,65536 /SUBSYSTEM:CONSOLE,5.02 /HIGHENTROPYVA /DYNAMICBASE /NXCOMPAT /CETCOMPAT /GUARD:CF /MANIFEST:EMBED /MANIFESTINPUT:asm/x86_64/wincmd64.manifest /ENTRY:mainCRTStartup "$BUILD_DIR/nasm/x86_64/u_${u_src}.obj" "$BUILD_DIR/libs/kernel32_x64.lib" "/OUT:$BUILD_DIR/nasm/x86_64/${u_out}"
    done
  fi
  if has_tc fasm; then
    mkdir -p "$BUILD_DIR/fasm/x86_64" "$BUILD_DIR/fasm-coff/x86_64"
    fasm "$DIST_DIR/fasm/x86_64/wincmd64.asm" "$BUILD_DIR/fasm/x86_64/WINCMD64.EXE" >/dev/null
    fasm "$DIST_DIR/fasm-coff/x86_64/wincmd64.asm" "$BUILD_DIR/fasm-coff/x86_64/wincmd64.obj" >/dev/null
    lld-link /NOLOGO /NODEFAULTLIB /BREPRO /STACK:1048576,65536 /SUBSYSTEM:CONSOLE,5.02 /HIGHENTROPYVA /DYNAMICBASE /NXCOMPAT /CETCOMPAT /GUARD:CF /MANIFEST:EMBED /MANIFESTINPUT:asm/x86_64/wincmd64.manifest /ENTRY:mainCRTStartup "$BUILD_DIR/fasm-coff/x86_64/wincmd64.obj" "$BUILD_DIR/libs/kernel32_x64.lib" "/OUT:$BUILD_DIR/fasm-coff/x86_64/WINCMD64.EXE"
    for pair in "${PE_UTILS[@]}"; do
      u_src="${pair%%:*}"
      u_out="${pair##*:}"
      fasm "$DIST_DIR/fasm/x86_64/util/${u_src}.asm" "$BUILD_DIR/fasm/x86_64/${u_out}" >/dev/null
      fasm "$DIST_DIR/fasm-coff/x86_64/util/${u_src}.asm" "$BUILD_DIR/fasm-coff/x86_64/u_${u_src}.obj" >/dev/null
      lld-link /NOLOGO /NODEFAULTLIB /BREPRO /STACK:1048576,65536 /SUBSYSTEM:CONSOLE,5.02 /HIGHENTROPYVA /DYNAMICBASE /NXCOMPAT /CETCOMPAT /GUARD:CF /MANIFEST:EMBED /MANIFESTINPUT:asm/x86_64/wincmd64.manifest /ENTRY:mainCRTStartup "$BUILD_DIR/fasm-coff/x86_64/u_${u_src}.obj" "$BUILD_DIR/libs/kernel32_x64.lib" "/OUT:$BUILD_DIR/fasm-coff/x86_64/${u_out}"
    done
  fi
  if has_tc jwasm; then
    mkdir -p "$BUILD_DIR/jwasm/x86_64"
    jwasm "${JWASM_WARN[@]}" -I"$DIST_DIR/jwasm/x86_64" -win64 -Fl="$BUILD_DIR/jwasm/x86_64/WINCMD64.LST" -Fo"$BUILD_DIR/jwasm/x86_64/wincmd64.obj" "$DIST_DIR/jwasm/x86_64/wincmd64.asm"
    lld-link /NOLOGO /NODEFAULTLIB /BREPRO /STACK:1048576,65536 /SUBSYSTEM:CONSOLE,5.02 /HIGHENTROPYVA /DYNAMICBASE /NXCOMPAT /CETCOMPAT /GUARD:CF /MANIFEST:EMBED /MANIFESTINPUT:asm/x86_64/wincmd64.manifest /ENTRY:mainCRTStartup "/MAP:$BUILD_DIR/jwasm/x86_64/WINCMD64.MAP" "$BUILD_DIR/jwasm/x86_64/wincmd64.obj" "$BUILD_DIR/libs/kernel32_x64.lib" "/OUT:$BUILD_DIR/jwasm/x86_64/WINCMD64.EXE"
    for pair in "${PE_UTILS[@]}"; do
      u_src="${pair%%:*}"
      u_out="${pair##*:}"
      jwasm "${JWASM_WARN[@]}" -I"$DIST_DIR/jwasm/x86_64/util" -win64 -Fo"$BUILD_DIR/jwasm/x86_64/u_${u_src}.obj" "$DIST_DIR/jwasm/x86_64/util/${u_src}.asm"
      lld-link /NOLOGO /NODEFAULTLIB /BREPRO /STACK:1048576,65536 /SUBSYSTEM:CONSOLE,5.02 /HIGHENTROPYVA /DYNAMICBASE /NXCOMPAT /CETCOMPAT /GUARD:CF /MANIFEST:EMBED /MANIFESTINPUT:asm/x86_64/wincmd64.manifest /ENTRY:mainCRTStartup "$BUILD_DIR/jwasm/x86_64/u_${u_src}.obj" "$BUILD_DIR/libs/kernel32_x64.lib" "/OUT:$BUILD_DIR/jwasm/x86_64/${u_out}"
    done
  fi
}

build_arm32() {
  if has_tc fasmarm; then
    mkdir -p "$BUILD_DIR/fasmarm/arm32"
    fasmarm "$DIST_DIR/fasmarm/arm32/v4/wincmdv4.asm" "$BUILD_DIR/fasmarm/arm32/WINCMD_ARMV4.EXE" >/dev/null
    fasmarm "$DIST_DIR/fasmarm/arm32/v7/wincmdv7.asm" "$BUILD_DIR/fasmarm/arm32/WINCMD_ARM32.EXE" >/dev/null
    for pair in "${PE_UTILS[@]}"; do
      u_src="${pair%%:*}"
      u_out="${pair##*:}"
      fasmarm "$DIST_DIR/fasmarm/arm32/util/${u_src}.asm" "$BUILD_DIR/fasmarm/arm32/${u_out}" >/dev/null
    done
  fi
  if has_tc llvm; then
    mkdir -p "$BUILD_DIR/llvm/arm32"
    llvm-mc -triple=armv4t-none-eabi --fatal-warnings -I "$DIST_DIR/llvm-mc/arm32/v4" -filetype=obj "$DIST_DIR/llvm-mc/arm32/v4/wincmdv4.asm" -o "$BUILD_DIR/llvm/arm32/wincmdv4.obj"
    llvm-mc -triple=thumbv7a-pc-windows-msvc -mattr=+hwdiv --fatal-warnings -I "$DIST_DIR/llvm-mc/arm32/v7" -filetype=obj "$DIST_DIR/llvm-mc/arm32/v7/wincmdv7.asm" -o "$BUILD_DIR/llvm/arm32/wincmdv7.obj"
    lld-link /WX /NOLOGO /NODEFAULTLIB /BREPRO /STACK:1048576,65536 /SUBSYSTEM:CONSOLE,6.02 /DYNAMICBASE /NXCOMPAT /GUARD:CF /MANIFEST:EMBED /MANIFESTINPUT:asm/x86_64/wincmd64.manifest /ENTRY:mainCRTStartup "/MAP:$BUILD_DIR/llvm/arm32/WINCMD_ARM32.MAP" "$BUILD_DIR/llvm/arm32/wincmdv7.obj" "$BUILD_DIR/libs/kernel32_arm32.lib" "/OUT:$BUILD_DIR/llvm/arm32/WINCMD_ARM32.EXE"
    for pair in "${PE_UTILS[@]}"; do
      u_src="${pair%%:*}"
      u_out="${pair##*:}"
      llvm-mc -triple=thumbv7a-pc-windows-msvc -mattr=+hwdiv --fatal-warnings -I "$DIST_DIR/llvm-mc/arm32/util" -filetype=obj "$DIST_DIR/llvm-mc/arm32/util/${u_src}.asm" -o "$BUILD_DIR/llvm/arm32/u_${u_src}.obj"
      lld-link /WX /NOLOGO /NODEFAULTLIB /BREPRO /STACK:1048576,65536 /SUBSYSTEM:CONSOLE,6.02 /DYNAMICBASE /NXCOMPAT /GUARD:CF /MANIFEST:EMBED /MANIFESTINPUT:asm/x86_64/wincmd64.manifest /ENTRY:mainCRTStartup "$BUILD_DIR/llvm/arm32/u_${u_src}.obj" "$BUILD_DIR/libs/kernel32_arm32.lib" "/OUT:$BUILD_DIR/llvm/arm32/${u_out}"
    done
  fi
}

build_arm64() {
  if has_tc fasmarm; then
    mkdir -p "$BUILD_DIR/fasmarm/arm64"
    fasmarm "$DIST_DIR/fasmarm/arm64/wincmda64.asm" "$BUILD_DIR/fasmarm/arm64/WINCMD_ARM64.EXE" >/dev/null
    for pair in "${PE_UTILS[@]}"; do
      u_src="${pair%%:*}"
      u_out="${pair##*:}"
      fasmarm "$DIST_DIR/fasmarm/arm64/util/${u_src}.asm" "$BUILD_DIR/fasmarm/arm64/${u_out}" >/dev/null
    done
  fi
  if has_tc llvm; then
    mkdir -p "$BUILD_DIR/llvm/arm64"
    llvm-mc -triple=aarch64-pc-windows-msvc --fatal-warnings -I "$DIST_DIR/llvm-mc/arm64" -filetype=obj "$DIST_DIR/llvm-mc/arm64/wincmda64.asm" -o "$BUILD_DIR/llvm/arm64/wincmda64.obj"
    lld-link /WX /NOLOGO /NODEFAULTLIB /BREPRO /STACK:1048576,65536 /SUBSYSTEM:CONSOLE,10.0 /HIGHENTROPYVA /DYNAMICBASE /NXCOMPAT /GUARD:CF /MANIFEST:EMBED /MANIFESTINPUT:asm/x86_64/wincmd64.manifest /ENTRY:mainCRTStartup "/MAP:$BUILD_DIR/llvm/arm64/WINCMD_ARM64.MAP" "$BUILD_DIR/llvm/arm64/wincmda64.obj" "$BUILD_DIR/libs/kernel32_arm64.lib" "/OUT:$BUILD_DIR/llvm/arm64/WINCMD_ARM64.EXE"
    for pair in "${PE_UTILS[@]}"; do
      u_src="${pair%%:*}"
      u_out="${pair##*:}"
      llvm-mc -triple=aarch64-pc-windows-msvc --fatal-warnings -I "$DIST_DIR/llvm-mc/arm64/util" -filetype=obj "$DIST_DIR/llvm-mc/arm64/util/${u_src}.asm" -o "$BUILD_DIR/llvm/arm64/u_${u_src}.obj"
      lld-link /WX /NOLOGO /NODEFAULTLIB /BREPRO /STACK:1048576,65536 /SUBSYSTEM:CONSOLE,10.0 /HIGHENTROPYVA /DYNAMICBASE /NXCOMPAT /GUARD:CF /MANIFEST:EMBED /MANIFESTINPUT:asm/x86_64/wincmd64.manifest /ENTRY:mainCRTStartup "$BUILD_DIR/llvm/arm64/u_${u_src}.obj" "$BUILD_DIR/libs/kernel32_arm64.lib" "/OUT:$BUILD_DIR/llvm/arm64/${u_out}"
    done
  fi
}

ASM_LINT_BIN=""
ensure_asm_lint_bin() {
  if [ -z "$ASM_LINT_BIN" ]; then
    cargo build --release --quiet -p asm-lint
    local tgt_dir
    tgt_dir="$(cargo metadata --format-version=1 --no-deps | sed -n 's/.*"target_directory":"\([^"]*\)".*/\1/p')"
    ASM_LINT_BIN="${tgt_dir:-target}/release/asm-lint"
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
    "$ASM_LINT_BIN" --equiv "$label" "${files[@]}"
  fi
}

verify_all() {
  ensure_asm_lint_bin
  if has_target dos16; then
    verify_equiv_group WINCMD.COM dos16 WINCMD.COM
    verify_equiv_group WINCMD1.COM dos16 WINCMD1.COM
    verify_equiv_group WINCMD.MSG dos16 WINCMD.MSG
    verify_equiv_group ASMCONV.COM dos16 ASMCONV.COM
    verify_equiv_group WINCMDMZ.EXE dos16 WINCMDMZ.EXE
    for pair in "${DOS16_UTILS[@]}"; do
      u_out="${pair##*:}"
      verify_equiv_group "$u_out" dos16 "$u_out"
    done
    for tc in nasm fasm jwasm; do
      if has_tc "$tc" && [ -f "$BUILD_DIR/$tc/dos16/WINCMD.COM" ]; then
        "$ASM_LINT_BIN" --budget "$BUILD_DIR/$tc/dos16/WINCMD.COM" 49152
        "$ASM_LINT_BIN" --budget "$BUILD_DIR/$tc/dos16/WINCMD1.COM" 20480
        "$ASM_LINT_BIN" --budget "$BUILD_DIR/$tc/dos16/WINCMD.MSG" 4096
        "$ASM_LINT_BIN" --budget "$BUILD_DIR/$tc/dos16/ASMCONV.COM" 4096
        if [ -f "$BUILD_DIR/$tc/dos16/WINCMDMZ.EXE" ]; then
          "$ASM_LINT_BIN" --purity 8086 "$BUILD_DIR/$tc/dos16/WINCMDMZ.EXE"
          "$ASM_LINT_BIN" --budget "$BUILD_DIR/$tc/dos16/WINCMDMZ.EXE" 4096
        fi
        util_sum=0
        for pair in "${DOS16_UTILS[@]}"; do
          u_out="${pair##*:}"
          "$ASM_LINT_BIN" --budget "$BUILD_DIR/$tc/dos16/${u_out}" 12288
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
        "$ASM_LINT_BIN" --purity 8086 "$BUILD_DIR/$tc/win16/WINCMD16.EXE"
        "$ASM_LINT_BIN" --purity 286 "$BUILD_DIR/$tc/win16/WINCMD286.EXE"
        "$ASM_LINT_BIN" --budget "$BUILD_DIR/$tc/win16/WINCMD.PIF" 545
        "$ASM_LINT_BIN" --budget "$BUILD_DIR/$tc/win16/WINCMD16.EXE" 65536
        "$ASM_LINT_BIN" --budget "$BUILD_DIR/$tc/win16/WINCMD286.EXE" 65536
      fi
    done
  fi
  if has_target x86_32; then
    verify_equiv_group WINCMD32.EXE x86_32 WINCMD32.EXE
    for pair in "${PE_UTILS[@]}"; do
      u_out="${pair##*:}"
      verify_equiv_group "x86_32/$u_out" x86_32 "$u_out"
    done
    for tc in nasm fasm fasm-pe32 jwasm; do
      tc_key="$tc"
      [ "$tc" = "fasm-pe32" ] && tc_key="fasm"
      if has_tc "$tc_key" && [ -f "$BUILD_DIR/$tc/x86_32/WINCMD32.EXE" ]; then
        "$ASM_LINT_BIN" --purity 386 "$BUILD_DIR/$tc/x86_32/WINCMD32.EXE"
        "$ASM_LINT_BIN" --budget "$BUILD_DIR/$tc/x86_32/WINCMD32.EXE" 131072
        for pair in "${PE_UTILS[@]}"; do
          u_out="${pair##*:}"
          "$ASM_LINT_BIN" --purity 386 "$BUILD_DIR/$tc/x86_32/${u_out}"
          "$ASM_LINT_BIN" --budget "$BUILD_DIR/$tc/x86_32/${u_out}" 32768
        done
      fi
    done
  fi
  if has_target x86_64; then
    verify_equiv_group WINCMD64.EXE x86_64 WINCMD64.EXE
    for pair in "${PE_UTILS[@]}"; do
      u_out="${pair##*:}"
      verify_equiv_group "x86_64/$u_out" x86_64 "$u_out"
    done
    for tc in nasm fasm jwasm; do
      if has_tc "$tc" && [ -f "$BUILD_DIR/$tc/x86_64/WINCMD64.EXE" ]; then
        "$ASM_LINT_BIN" --purity x64 "$BUILD_DIR/$tc/x86_64/WINCMD64.EXE"
        "$ASM_LINT_BIN" --budget "$BUILD_DIR/$tc/x86_64/WINCMD64.EXE" 131072
        for pair in "${PE_UTILS[@]}"; do
          u_out="${pair##*:}"
          "$ASM_LINT_BIN" --purity x64 "$BUILD_DIR/$tc/x86_64/${u_out}"
          "$ASM_LINT_BIN" --budget "$BUILD_DIR/$tc/x86_64/${u_out}" 32768
        done
      fi
    done
    if has_tc fasm && [ -f "$BUILD_DIR/fasm-coff/x86_64/WINCMD64.EXE" ]; then
      "$ASM_LINT_BIN" --budget "$BUILD_DIR/fasm-coff/x86_64/WINCMD64.EXE" 131072
      for pair in "${PE_UTILS[@]}"; do
        u_out="${pair##*:}"
        "$ASM_LINT_BIN" --budget "$BUILD_DIR/fasm-coff/x86_64/${u_out}" 32768
      done
    fi
  fi
  if has_target arm32; then
    if has_tc fasmarm && [ -f "$BUILD_DIR/fasmarm/arm32/WINCMD_ARMV4.EXE" ]; then
      "$ASM_LINT_BIN" --purity armv4 "$BUILD_DIR/fasmarm/arm32/WINCMD_ARMV4.EXE"
      "$ASM_LINT_BIN" --budget "$BUILD_DIR/fasmarm/arm32/WINCMD_ARMV4.EXE" 131072
    fi
    for tc in fasmarm llvm; do
      if has_tc "$tc" && [ -f "$BUILD_DIR/$tc/arm32/WINCMD_ARM32.EXE" ]; then
        "$ASM_LINT_BIN" --purity armv7 "$BUILD_DIR/$tc/arm32/WINCMD_ARM32.EXE"
        "$ASM_LINT_BIN" --budget "$BUILD_DIR/$tc/arm32/WINCMD_ARM32.EXE" 131072
        for pair in "${PE_UTILS[@]}"; do
          u_out="${pair##*:}"
          "$ASM_LINT_BIN" --purity armv7 "$BUILD_DIR/$tc/arm32/${u_out}"
          "$ASM_LINT_BIN" --budget "$BUILD_DIR/$tc/arm32/${u_out}" 32768
        done
      fi
    done
  fi
  if has_target arm64; then
    for tc in fasmarm llvm; do
      if has_tc "$tc" && [ -f "$BUILD_DIR/$tc/arm64/WINCMD_ARM64.EXE" ]; then
        "$ASM_LINT_BIN" --purity arm64 "$BUILD_DIR/$tc/arm64/WINCMD_ARM64.EXE"
        "$ASM_LINT_BIN" --budget "$BUILD_DIR/$tc/arm64/WINCMD_ARM64.EXE" 131072
        for pair in "${PE_UTILS[@]}"; do
          u_out="${pair##*:}"
          "$ASM_LINT_BIN" --purity arm64 "$BUILD_DIR/$tc/arm64/${u_out}"
          "$ASM_LINT_BIN" --budget "$BUILD_DIR/$tc/arm64/${u_out}" 32768
        done
      fi
    done
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
  package)
    "$0" all
    cargo run --release --quiet -p media-pkg -- --root . --build-dir "$BUILD_DIR" --dist-src "$DIST_DIR" --out-dir dist/release
    ;;
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
      dos_coms=()
      for tc in nasm fasm jwasm; do
        [ -f "$BUILD_DIR/$tc/dos16/WINCMD.COM" ] && dos_coms+=("$BUILD_DIR/$tc/dos16/WINCMD.COM")
      done
      if [ "${#dos_coms[@]}" -ge 2 ]; then
        cargo run --release --quiet -p diff-fuzz -- --era "$era" --cases 500 "${dos_coms[@]}"
      fi
    done
    mkdir -p "$BUILD_DIR/lcov"
    cargo run --release --quiet -p emu-cov -- --com "$BUILD_DIR/nasm/dos16/WINCMD.COM" --era 7.10 --args " /C VER" --lcov "$BUILD_DIR/lcov/dos16.info"
    cargo run --release --quiet -p emu-cov -- --pe "$BUILD_DIR/nasm/x86_32/WINCMD32.EXE" --args "/C VER" --lcov "$BUILD_DIR/lcov/x86_32.info"
    cargo run --release --quiet -p emu-cov -- --pe "$BUILD_DIR/nasm/x86_64/WINCMD64.EXE" --args "/C VER" --lcov "$BUILD_DIR/lcov/x86_64.info"
    if [ -f "$BUILD_DIR/fasmarm/arm32/WINCMD_ARMV4.EXE" ]; then
      cargo run --release --quiet -p emu-cov -- --pe "$BUILD_DIR/fasmarm/arm32/WINCMD_ARMV4.EXE" --args "/C VER" --lcov "$BUILD_DIR/lcov/armv4.info"
    fi
    arm32_pe="$BUILD_DIR/fasmarm/arm32/WINCMD_ARM32.EXE"
    [ -f "$arm32_pe" ] || arm32_pe="$BUILD_DIR/llvm/arm32/WINCMD_ARM32.EXE"
    cargo run --release --quiet -p emu-cov -- --pe "$arm32_pe" --args "/C VER" --lcov "$BUILD_DIR/lcov/arm32.info"
    arm64_pe="$BUILD_DIR/fasmarm/arm64/WINCMD_ARM64.EXE"
    [ -f "$arm64_pe" ] || arm64_pe="$BUILD_DIR/llvm/arm64/WINCMD_ARM64.EXE"
    cargo run --release --quiet -p emu-cov -- --pe "$arm64_pe" --args "/C VER" --lcov "$BUILD_DIR/lcov/arm64.info"
    cat "$BUILD_DIR/lcov/"*.info > lcov.info
    cargo run --release --quiet -p diff-fuzz -- --cross-isa --seed 0x20261123 --cases 250 --build-dir "$BUILD_DIR"
    if command -v wine >/dev/null 2>&1; then
      [ -f "$BUILD_DIR/nasm/x86_32/WINCMD32.EXE" ] && WINEDEBUG=-all wine "$BUILD_DIR/nasm/x86_32/WINCMD32.EXE" /C VER
      [ -f "$BUILD_DIR/fasm/x86_64/WINCMD64.EXE" ] && WINEDEBUG=-all wine "$BUILD_DIR/fasm/x86_64/WINCMD64.EXE" /C VER
      [ -f "$BUILD_DIR/jwasm/x86_64/WINCMD64.EXE" ] && WINEDEBUG=-all wine "$BUILD_DIR/jwasm/x86_64/WINCMD64.EXE" /C VER
    fi
    ;;
  fuzz)
    "$0" all
    for era in 1.25 2.00 4.00 7.10; do
      cargo run --release --quiet -p diff-fuzz -- --era "$era" --cases 2500 "$BUILD_DIR/nasm/dos16/WINCMD.COM" "$BUILD_DIR/fasm/dos16/WINCMD.COM" "$BUILD_DIR/jwasm/dos16/WINCMD.COM"
    done
    cargo run --release --quiet -p diff-fuzz -- --cross-isa --seed 0x20261123 --cases 10000 --ref-cmd 25 --build-dir "$BUILD_DIR"
    ;;
  *)
    echo "Unknown mode: $MODE" >&2
    exit 1
    ;;
esac
