#!/usr/bin/env bash
# Static ABI verification of a built binary against the verified RG35XX (OG) target.
# Read-only. Usage: check-binary.sh <binary> [native|core]
set -u
SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib.sh"

[ "$#" -ge 1 ] || die "usage: $0 <binary> [native|core]"
BIN="$1"
KIND="${2:-native}"
[ -f "$BIN" ] || die "not a file: $BIN"

fail=0
check() { # description, result (0=ok)
  if [ "$2" -eq 0 ]; then printf 'PASS: %s\n' "$1"; else printf 'FAIL: %s\n' "$1" >&2; fail=1; fi
}

printf '=== file ===\n'
if command -v file >/dev/null 2>&1; then file "$BIN"; else printf '(file not available)\n'; fi

printf '=== ELF header ===\n'
if ! command -v readelf >/dev/null 2>&1; then
  warn "readelf not available; static ABI checks are limited"
else
  HDR="$(readelf -h "$BIN" 2>/dev/null)"
  printf '%s\n' "$HDR" | grep -E 'Class:|Data:|Type:|Machine:|Flags:' || true
  printf '%s\n' "$HDR" | grep -q 'Class:.*ELF32';  check "ELF32" $?
  printf '%s\n' "$HDR" | grep -q 'Machine:.*ARM';  check "ARM machine" $?
  printf '%s\n' "$HDR" | grep -q 'EABI';          check "EABI flags" $?
  if [ "$KIND" = "core" ]; then
    printf '%s\n' "$HDR" | grep -q 'Type:.*DYN'; check "ET_DYN (libretro core)" $?
  else
    # The pinned SDK's GCC 11.4.0 emits PIE executables by default: ELF type
    # ET_DYN together with a PT_INTERP of /lib/ld-linux-armhf.so.3. PIE is a
    # normal native executable on armhf glibc Linux, so both ET_EXEC and
    # ET_DYN are valid here. The hard-float interpreter check below remains
    # the strong guard that distinguishes an executable from a shared object.
    if printf '%s\n' "$HDR" | grep -q 'Type:.*EXEC'; then
      check "ET_EXEC (native executable)" 0
    elif printf '%s\n' "$HDR" | grep -q 'Type:.*DYN'; then
      check "ET_DYN PIE (native executable)" 0
    else
      check "native executable ELF type (EXEC or DYN/PIE)" 1
    fi
  fi
fi

printf '=== interpreter ===\n'
INTERP=""
if command -v readelf >/dev/null 2>&1; then
  INTERP="$(readelf -l "$BIN" 2>/dev/null | grep -i interpreter | sed 's/.*\[\(.*\)\].*/\1/')"
elif command -v strings >/dev/null 2>&1; then
  INTERP="$(strings "$BIN" | grep -m1 'ld-linux' || true)"
fi
printf 'interpreter: %s\n' "${INTERP:-<none>}"
if printf '%s' "$INTERP" | grep -q 'ld-linux-armhf.so.3'; then
  check "hard-float armhf interpreter" 0
elif [ "$KIND" = "core" ]; then
  printf 'NOTE: shared objects may have no interpreter\n'
else
  warn "no armhf interpreter detected"
  check "hard-float armhf interpreter" 1
fi

printf '=== GLIBC symbol versions ===\n'
GLIBC_MAX=""
if command -v strings >/dev/null 2>&1; then
  GLIBC_MAX="$(strings "$BIN" 2>/dev/null | grep -o 'GLIBC_[0-9][0-9.]*' | sort -u | \
    awk -F_ '{split($2,a,"."); printf "%d %d %d %s\n", a[1]+0, a[2]+0, a[3]+0, $0}' | \
    sort -n -k1,1 -k2,2 -k3,3 | tail -1 | awk '{print $4}')"
fi
printf 'max GLIBC symbol: %s\n' "${GLIBC_MAX:-<none>}"
if [ -n "$GLIBC_MAX" ]; then
  M="${GLIBC_MAX#GLIBC_}"
  MAJ="${M%%.*}"
  MIN="${M#*.}"; MIN="${MIN%%.*}"
  [ "$MAJ" -le 2 ]; check "GLIBC major <= 2" $?
  if [ "$MAJ" -eq 2 ] && [ "${MIN:-0}" -le 37 ]; then
    check "GLIBC <= device $DEVICE_GLIBC_MAX" 0
  else
    check "GLIBC <= device $DEVICE_GLIBC_MAX" 1
  fi
fi

printf '=== dynamic dependencies ===\n'
if command -v readelf >/dev/null 2>&1; then
  readelf -d "$BIN" 2>/dev/null | grep -i 'NEEDED' || printf '(none / static)\n'
elif command -v objdump >/dev/null 2>&1; then
  objdump -p "$BIN" 2>/dev/null | grep 'NEEDED' || printf '(none / static)\n'
else
  printf '(no readelf/objdump available)\n'
fi

printf '=== result ===\n'
if [ "$fail" -eq 0 ]; then
  printf 'BINARY CHECK PASSED (still requires on-device runtime validation)\n'
else
  printf 'BINARY CHECK FAILED\n' >&2
fi
exit "$fail"
