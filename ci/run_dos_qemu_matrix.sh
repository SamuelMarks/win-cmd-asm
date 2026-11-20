#!/bin/sh
# @file ci/run_dos_qemu_matrix.sh
# @brief Headless QEMU verification matrix for WINCMD.COM / ASMCONV.COM across MS-DOS 1.25, 2.00, 4.00 & FreeDOS 1.3.
# @description
#   Modeled after SamuelMarks/c-ci/run_dos_watcom_matrix.sh. Boots genuine
#   Bento DOS images under headless QEMU, uploads built DOS16 binaries via
#   SERGET.COM, and verifies live execution.
set -eu

SCRIPT_DIR="$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)"
REPO_ROOT="$(CDPATH='' cd -- "$SCRIPT_DIR/.." && pwd)"

OS_LIST="${DOS_MATRIX_OS:-msdos-4.0,msdos-2.0,msdos-1.25,freedos}"
TOOLCHAIN="${TOOLCHAIN:-nasm}"
BENTO_DIR_ARG="${BENTO_DIR:-}"
BUILD_DIR_ARG="${BUILD_DIR:-$REPO_ROOT/build}"

usage() {
  cat << 'EOF'
Usage: sh ./ci/run_dos_qemu_matrix.sh [OPTIONS]

Executes WINCMD.COM, WINCMD.MSG, and ASMCONV.COM inside real headless QEMU
MS-DOS (1.25, 2.00, 4.00) and FreeDOS 1.3 guests using SamuelMarks/bento.

Options:
  --os <list>         Comma-separated targets: msdos-4.0,msdos-2.0,msdos-1.25,freedos, msdos, or all
  --toolchain <name>  Assembler build directory to test: nasm, fasm, or jwasm (default: nasm)
  --bento-dir <path>  Path to SamuelMarks/bento checkout (default: $BENTO_DIR or ../bento)
  --build-dir <path>  Path to win-cmd-asm build directory (default: ./build)
  -h, --help          Show this help message
EOF
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --os) OS_LIST="$2"; shift 2 ;;
    --toolchain) TOOLCHAIN="$2"; shift 2 ;;
    --bento-dir) BENTO_DIR_ARG="$2"; shift 2 ;;
    --build-dir) BUILD_DIR_ARG="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

if [ -z "$BENTO_DIR_ARG" ]; then
  if [ -d "$REPO_ROOT/../bento" ]; then
    BENTO_DIR_ARG="$REPO_ROOT/../bento"
  else
    echo "ERROR: Bento checkout not found. Pass --bento-dir <path> or set BENTO_DIR." >&2
    exit 1
  fi
fi

case "$OS_LIST" in
  all) OS_LIST="msdos-4.0,msdos-2.0,msdos-1.25,freedos" ;;
  msdos) OS_LIST="msdos-4.0,msdos-2.0,msdos-1.25" ;;
esac

command -v ruby >/dev/null 2>&1 || { echo "ERROR: ruby is required in PATH" >&2; exit 1; }

OLD_IFS="$IFS"
IFS=','
for raw_os in $OS_LIST; do
  IFS="$OLD_IFS"
  case "$raw_os" in
    4.0|4.00|msdos-4.0) target="msdos-4.0" ;;
    2.0|2.00|msdos-2.0) target="msdos-2.0" ;;
    1.25|msdos-1.25) target="msdos-1.25" ;;
    freedos|freedos-1.3) target="freedos" ;;
    *) echo "ERROR: Unsupported DOS target '$raw_os'" >&2; exit 2 ;;
  esac
  echo "=== [dos-qemu-matrix] Running target=$target toolchain=$TOOLCHAIN ==="
  WINCMD_DOS_OS="$target" BENTO_DIR="$BENTO_DIR_ARG" BUILD_DIR="$BUILD_DIR_ARG" TOOLCHAIN="$TOOLCHAIN" ruby "$SCRIPT_DIR/dos_qemu_ci.rb"
  IFS=','
done
IFS="$OLD_IFS"

echo "=== [dos-qemu-matrix] All requested QEMU DOS targets passed ==="
