#!/usr/bin/env bash
# Project-local SDK environment. Source this file; never added to shell rc / global profile.
# Usage: . scripts/koriki-env.sh
source "$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/lib.sh"

[ -d "$SDK_DIR" ] || die "SDK not found at $SDK_DIR (run scripts/setup-sdk.sh or set BROGUE_SDK)"

export SDK="$SDK_DIR"
export PATH="$SDK/bin:$PATH"
export CROSS_COMPILE="$TARGET_PREFIX"
export CC="${TARGET_PREFIX}gcc"
export CXX="${TARGET_PREFIX}g++"
export SYSROOT="$SDK/$SDK_TUPLE/sysroot"
export PKG_CONFIG="$SDK/bin/pkg-config"
export PKG_CONFIG_SYSROOT_DIR="$SYSROOT"
export PKG_CONFIG_LIBDIR="$SYSROOT/usr/lib/pkgconfig:$SYSROOT/usr/share/pkgconfig:$SYSROOT/usr/lib/$SDK_TUPLE/pkgconfig"
export PKG_CONFIG_PATH=""
export SDL_CONFIG="$PROJECT_ROOT/scripts/sdl2-config"
export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
