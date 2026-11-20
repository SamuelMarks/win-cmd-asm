#!/bin/sh
# @file ci/check_eol.sh
# @brief Verifies repository CRLF/LF line-ending policy matching .githooks/pre-commit.
set -eu

REPO_ROOT="$(git rev-parse --show-toplevel)"
cd "$REPO_ROOT"

classify_eol() {
  f_path="$1"
  f_base="${f_path##*/}"
  case "$f_base" in
    AUTOEXEC.BAT|autoexec.bat|CONFIG.SYS|config.sys)
      echo "crlf"
      return 0
      ;;
  esac
  case "$f_path" in
    *.bin|*.BIN|*.com|*.COM|*.exe|*.EXE|*.dll|*.DLL|*.so|*.dylib) echo "binary"; return 0 ;;
    *.sys|*.SYS|*.drv|*.DRV|*.vxd|*.VXD|*.386|*.ovl|*.OVL|*.ovr|*.OVR) echo "binary"; return 0 ;;
    *.pif|*.PIF|*.scr|*.SCR|*.cpl|*.CPL|*.ocx|*.OCX|*.efi|*.EFI) echo "binary"; return 0 ;;
    *.rom|*.ROM|*.elf|*.ELF|*.o|*.O|*.obj|*.OBJ|*.omf|*.OMF|*.a) echo "binary"; return 0 ;;
    *.lib|*.LIB|*.exp|*.EXP|*.res|*.RES|*.pch|*.pdb|*.PDB|*.ilk|*.ILK) echo "binary"; return 0 ;;
    *.dbg|*.DBG|*.sym|*.SYM|*.msg|*.MSG|*.bsc|*.sbr) echo "binary"; return 0 ;;
    *.img|*.IMG|*.ima|*.IMA|*.flp|*.FLP|*.vfd|*.VFD|*.dsk|*.DSK) echo "binary"; return 0 ;;
    *.iso|*.ISO|*.vhd|*.vhdx|*.vmdk|*.vdi|*.qcow|*.qcow2|*.dmg) echo "binary"; return 0 ;;
    *.ico|*.ICO|*.cur|*.CUR|*.bmp|*.BMP|*.png|*.PNG|*.jpg|*.JPG) echo "binary"; return 0 ;;
    *.jpeg|*.JPEG|*.gif|*.GIF|*.webp|*.woff|*.woff2|*.ttf|*.otf) echo "binary"; return 0 ;;
    *.fon|*.fnt|*.pdf) echo "binary"; return 0 ;;
    *.zip|*.ZIP|*.gz|*.tgz|*.bz2|*.xz|*.zst|*.7z|*.cab|*.msi|*.rar|*.tar|*.arj|*.lzh) echo "binary"; return 0 ;;
    *.bat|*.BAT|*.cmd|*.CMD|*.btm|*.BTM) echo "crlf"; return 0 ;;
    *.reg|*.REG|*.inf|*.INF|*.ini|*.INI) echo "crlf"; return 0 ;;
    *.rc|*.RC|*.dlg|*.DLG|*.mc|*.MC) echo "crlf"; return 0 ;;
    *.ps1|*.psm1|*.psd1|*.vbs|*.VBS|*.wsf|*.WSF) echo "crlf"; return 0 ;;
    *.sln|*.vcxproj|*.vcxproj.filters|*.props|*.targets) echo "crlf"; return 0 ;;
    *) echo "lf"; return 0 ;;
  esac
}

TMP_DIR="${TMPDIR:-/tmp}/wincmd-eol-check.$$"
mkdir -p "$TMP_DIR"
cleanup() { rm -rf "$TMP_DIR"; }
trap cleanup EXIT INT TERM HUP

command -v dos2unix >/dev/null 2>&1 || { echo "ERROR: dos2unix is required" >&2; exit 1; }
command -v unix2dos >/dev/null 2>&1 || { echo "ERROR: unix2dos is required" >&2; exit 1; }

BAD=0
CHECKED=0
git ls-files > "$TMP_DIR/tracked.lst"

while IFS= read -r rel_file; do
  [ -n "$rel_file" ] || continue
  [ -f "$rel_file" ] || continue
  [ ! -L "$rel_file" ] || continue

  kind="$(classify_eol "$rel_file")"
  if [ "$kind" = "binary" ]; then
    continue
  fi

  CHECKED=$((CHECKED + 1))
  cp "$rel_file" "$TMP_DIR/expected.bin"
  if [ "$kind" = "crlf" ]; then
    unix2dos -q -- "$TMP_DIR/expected.bin"
  else
    dos2unix -q -- "$TMP_DIR/expected.bin"
  fi

  if ! cmp -s "$rel_file" "$TMP_DIR/expected.bin"; then
    echo "EOL violation ($kind expected): $rel_file" >&2
    BAD=$((BAD + 1))
  fi
done < "$TMP_DIR/tracked.lst"

if [ "$BAD" -ne 0 ]; then
  echo "check_eol: $BAD file(s) violated line-ending policy." >&2
  exit 1
fi

echo "check_eol: $CHECKED tracked text files conform to CRLF/LF policy."
