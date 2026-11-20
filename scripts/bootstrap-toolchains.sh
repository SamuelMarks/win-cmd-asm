#!/usr/bin/env bash
# Reproducible multi-assembler bootstrap helper for POSIX (macOS / Linux / MSYS2) hosts.
# Ensures NASM, JWasm, FASM, and FASMARM are callable from PATH or TC_ROOT/bin.
set -euo pipefail

TC_ROOT="${WINCMD_TC_DIR:-${TMPDIR:-/tmp}/win-cmd-asm-toolchains}"
if [ -z "${WINCMD_TC_DIR:-}" ] && [ -d "/Users/samuel/.gemini/tmp/win-cmd-asm/toolchains" ]; then
  TC_ROOT="/Users/samuel/.gemini/tmp/win-cmd-asm/toolchains"
fi

TOOLCHAIN_REQ="${TOOLCHAIN:-all}"
HOST_OS="$(uname -s)"
HOST_ARCH="$(uname -m)"

need_tool() {
  local tool="$1"
  [ "$TOOLCHAIN_REQ" = "all" ] && return 0
  case ",${TOOLCHAIN_REQ}," in
    *,"${tool}",*) return 0 ;;
    *) return 1 ;;
  esac
}

mkdir -p "$TC_ROOT/bin" "$TC_ROOT/src"

if need_tool jwasm && ! command -v jwasm >/dev/null 2>&1 && [ ! -x "$TC_ROOT/bin/jwasm" ]; then
  (
    cd "$TC_ROOT/src"
    if [ ! -d "JWasm-master" ]; then
      curl -fsSL https://github.com/Baron-von-Riedesel/JWasm/archive/refs/heads/master.tar.gz -o jwasm.tar.gz
      tar -xzf jwasm.tar.gz
    fi
    mkdir -p "$TC_ROOT/src/shim_inc"
    echo "#include <stdlib.h>" > "$TC_ROOT/src/shim_inc/malloc.h"
    SRCS=$(ls "$TC_ROOT/src/JWasm-master"/src/*.c | grep -v '/trmem\.c$')
    CC_BIN="${CC:-}"
    if [ -z "$CC_BIN" ]; then
      if command -v clang >/dev/null 2>&1; then
        CC_BIN=clang
      else
        CC_BIN=cc
      fi
    fi
    "$CC_BIN" -O2 -DNDEBUG -D__UNIX__ -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -w -I"$TC_ROOT/src/shim_inc" -I"$TC_ROOT/src/JWasm-master/src/H" -I"$TC_ROOT/src/JWasm-master/src" $SRCS -lm -o "$TC_ROOT/bin/jwasm"
  )
fi

if (need_tool fasm || need_tool fasmarm) && ! command -v fasm >/dev/null 2>&1 && [ ! -x "$TC_ROOT/bin/fasm" ]; then
  if [ "$HOST_OS" = "Linux" ] && [ "$HOST_ARCH" = "x86_64" ]; then
    if [ ! -x "$TC_ROOT/fasm-linux/fasm.x64" ]; then
      mkdir -p "$TC_ROOT/fasm-linux"
      curl -fsSL https://flatassembler.net/fasm-1.73.32.tgz -o "$TC_ROOT/src/fasm-linux.tgz"
      tar -xzf "$TC_ROOT/src/fasm-linux.tgz" -C "$TC_ROOT/fasm-linux" --strip-components=1
      chmod +x "$TC_ROOT/fasm-linux/fasm.x64"
    fi
    cat > "$TC_ROOT/bin/fasm" << 'EOF_LINUX_FASM'
#!/usr/bin/env bash
set -euo pipefail
TC_DIR="$(cd "$(dirname "$0")/.." && pwd)"
export INCLUDE="${INCLUDE:-$TC_DIR/fasm-linux/include}"
exec "$TC_DIR/fasm-linux/fasm.x64" "$@"
EOF_LINUX_FASM
    chmod +x "$TC_ROOT/bin/fasm"
  else
    if [ ! -f "$TC_ROOT/fasm/FASM.EXE" ]; then
      mkdir -p "$TC_ROOT/fasm"
      curl -fsSL https://flatassembler.net/fasmw17332.zip -o "$TC_ROOT/src/fasmw.zip"
      unzip -qo "$TC_ROOT/src/fasmw.zip" -d "$TC_ROOT/fasm"
    fi
    cat > "$TC_ROOT/bin/fasm" << 'EOF_WRAP'
#!/usr/bin/env bash
set -euo pipefail
export WINEDEBUG=-all
TC_DIR="$(cd "$(dirname "$0")/.." && pwd)"
WINE_CMD="$(command -v wine || echo /Users/samuel/.local/bin/wine)"
BS="$(printf '\134')"
TC_WIN="Z:$(printf '%s' "$TC_DIR" | tr '/' "$BS")"
export INCLUDE="${INCLUDE:-${TC_WIN}${BS}fasm${BS}INCLUDE}"
ARGS=()
for arg in "$@"; do
  case "$arg" in
    /*) ARGS+=("Z:$(printf '%s' "$arg" | tr '/' "$BS")") ;;
    *)  ARGS+=("$arg") ;;
  esac
done
exec "$WINE_CMD" "${TC_WIN}${BS}fasm${BS}FASM.EXE" "${ARGS[@]}"
EOF_WRAP
    chmod +x "$TC_ROOT/bin/fasm"
  fi
fi

if need_tool fasmarm && ! command -v fasmarm >/dev/null 2>&1 && [ ! -x "$TC_ROOT/bin/fasmarm" ]; then
  if [ "$HOST_OS" = "Linux" ] && [ "$HOST_ARCH" = "x86_64" ]; then
    if [ ! -x "$TC_ROOT/fasmarm-linux/fasmarm" ]; then
      mkdir -p "$TC_ROOT/fasmarm-linux"
      if [ ! -f "$TC_ROOT/src/fasmarm.zip" ]; then
        curl -fsSL https://arm.flatassembler.net/FASMARM_small.ZIP -o "$TC_ROOT/src/fasmarm.zip"
      fi
      unzip -qo "$TC_ROOT/src/fasmarm.zip" -d "$TC_ROOT/fasmarm-linux"
      if [ ! -d "$TC_ROOT/fasm-linux/source" ]; then
        mkdir -p "$TC_ROOT/fasm-linux"
        curl -fsSL https://flatassembler.net/fasm-1.73.32.tgz -o "$TC_ROOT/src/fasm-linux.tgz"
        tar -xzf "$TC_ROOT/src/fasm-linux.tgz" -C "$TC_ROOT/fasm-linux" --strip-components=1
        chmod +x "$TC_ROOT/fasm-linux/fasm.x64"
      fi
      cp -rn "$TC_ROOT/fasm-linux/source/"* "$TC_ROOT/fasmarm-linux/source/" 2>/dev/null || true
      (cd "$TC_ROOT/fasmarm-linux/source/linux/x64" && "$TC_ROOT/fasm-linux/fasm.x64" fasmarm.asm "$TC_ROOT/fasmarm-linux/fasmarm" >/dev/null)
      chmod +x "$TC_ROOT/fasmarm-linux/fasmarm"
    fi
    cat > "$TC_ROOT/bin/fasmarm" << 'EOF_LINUX_FASMARM'
#!/usr/bin/env bash
set -euo pipefail
TC_DIR="$(cd "$(dirname "$0")/.." && pwd)"
export INCLUDE="${INCLUDE:-$TC_DIR/fasm-linux/include;$TC_DIR/fasmarm-linux/include}"
exec "$TC_DIR/fasmarm-linux/fasmarm" "$@"
EOF_LINUX_FASMARM
    chmod +x "$TC_ROOT/bin/fasmarm"
  else
    if [ ! -f "$TC_ROOT/fasmarm/FASMARM.EXE" ]; then
      mkdir -p "$TC_ROOT/fasmarm"
      if [ ! -f "$TC_ROOT/src/fasmarm.zip" ]; then
        curl -fsSL https://arm.flatassembler.net/FASMARM_small.ZIP -o "$TC_ROOT/src/fasmarm.zip"
      fi
      unzip -qo "$TC_ROOT/src/fasmarm.zip" -d "$TC_ROOT/fasmarm"
      cp -rn "$TC_ROOT/fasm/SOURCE/"* "$TC_ROOT/fasmarm/source/" 2>/dev/null || true
      (cd "$TC_ROOT/fasmarm/source/Win32" && "$TC_ROOT/bin/fasm" fasmarm.asm "$TC_ROOT/fasmarm/FASMARM.EXE")
    fi
    cat > "$TC_ROOT/bin/fasmarm" << 'EOF_WRAP'
#!/usr/bin/env bash
set -euo pipefail
export WINEDEBUG=-all
TC_DIR="$(cd "$(dirname "$0")/.." && pwd)"
WINE_CMD="$(command -v wine || echo /Users/samuel/.local/bin/wine)"
BS="$(printf '\134')"
TC_WIN="Z:$(printf '%s' "$TC_DIR" | tr '/' "$BS")"
export INCLUDE="${INCLUDE:-${TC_WIN}${BS}fasm${BS}INCLUDE;${TC_WIN}${BS}fasmarm${BS}include}"
ARGS=()
for arg in "$@"; do
  case "$arg" in
    /*) ARGS+=("Z:$(printf '%s' "$arg" | tr '/' "$BS")") ;;
    *)  ARGS+=("$arg") ;;
  esac
done
exec "$WINE_CMD" "${TC_WIN}${BS}fasmarm${BS}FASMARM.EXE" "${ARGS[@]}"
EOF_WRAP
    chmod +x "$TC_ROOT/bin/fasmarm"
  fi
fi

echo "$TC_ROOT/bin"
