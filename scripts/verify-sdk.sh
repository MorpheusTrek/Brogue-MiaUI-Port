#!/usr/bin/env bash
# Verify an installed Koriki/Batocera SDK against the project's expectations. Read-only.
# Usage: verify-sdk.sh [--quiet] [SDK_DIR]
set -u
SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib.sh"

QUIET=0
SDK_TO_CHECK=""
for arg in "$@"; do
  case "$arg" in
    --quiet) QUIET=1 ;;
    *) SDK_TO_CHECK="$arg" ;;
  esac
done
[ -n "$SDK_TO_CHECK" ] || SDK_TO_CHECK="$SDK_DIR"

say() { [ "$QUIET" -eq 1 ] || printf '%s\n' "$*"; }
fail=0
check() { # description, result (0=ok)
  if [ "$2" -eq 0 ]; then say "PASS: $1"; else printf 'FAIL: %s\n' "$1" >&2; fail=1; fi
}

say "=== SDK verification: $SDK_TO_CHECK ==="
[ -d "$SDK_TO_CHECK" ] || die "SDK directory not found: $SDK_TO_CHECK"

GCC_BIN="$SDK_TO_CHECK/bin/${TARGET_PREFIX}gcc"
SYSROOT="$SDK_TO_CHECK/$SDK_TUPLE/sysroot"
PKG="$SDK_TO_CHECK/bin/pkg-config"

[ -x "$GCC_BIN" ]; check "cross compiler present ($GCC_BIN)" $?
[ "$fail" -eq 0 ] || exit 1

ACTUAL_GCC="$("$GCC_BIN" -dumpversion 2>/dev/null)"
say "compiler version: ${ACTUAL_GCC:-<unknown>}"
if [ "$ACTUAL_GCC" = "$EXPECTED_GCC" ]; then
  check "GCC $EXPECTED_GCC" 0
else
  warn "expected GCC $EXPECTED_GCC, found '${ACTUAL_GCC:-<unknown>}'"
  check "GCC $EXPECTED_GCC" 1
fi

REPORTED_SYSROOT="$("$GCC_BIN" -print-sysroot 2>/dev/null)"
say "compiler sysroot: ${REPORTED_SYSROOT:-<unknown>}"
[ -d "$SYSROOT" ]; check "sysroot directory exists" $?

[ -x "$PKG" ]; check "SDK pkg-config present" $?
if [ -x "$PKG" ]; then
  export PKG_CONFIG_SYSROOT_DIR="$SYSROOT"
  export PKG_CONFIG_LIBDIR="$SYSROOT/usr/lib/pkgconfig:$SYSROOT/usr/share/pkgconfig:$SYSROOT/usr/lib/$SDK_TUPLE/pkgconfig"
  export PKG_CONFIG_PATH=""
  SDL_VER="$("$PKG" --modversion sdl2 2>/dev/null)"
  say "SDL2 pkg-config version: ${SDL_VER:-<not found>}"
  [ -n "$SDL_VER" ]; check "SDL2 pkg-config package resolvable" $?
  IMG_H="$(find "$SYSROOT/usr/include" -name 'SDL_image.h' -print -quit 2>/dev/null)"
  [ -n "$IMG_H" ]; check "SDL2_image header present" $?
  IMG_LIB="$(find "$SYSROOT/usr/lib" -name 'libSDL2_image*' -print -quit 2>/dev/null)"
  [ -n "$IMG_LIB" ]; check "SDL2_image library present" $?
fi

if [ "$fail" -eq 0 ]; then
  say "SDK VERIFICATION PASSED"
else
  printf 'SDK VERIFICATION FAILED\n' >&2
fi
exit "$fail"
