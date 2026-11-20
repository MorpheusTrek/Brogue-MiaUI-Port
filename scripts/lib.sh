#!/usr/bin/env bash
# Shared helpers and constants. Source this from other bash scripts; do not execute directly.

if [ -n "${BASH_SOURCE[0]:-}" ]; then
  LIB_DIR="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
else
  LIB_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
fi
PROJECT_ROOT="$(CDPATH= cd -- "$LIB_DIR/.." && pwd)"

die()  { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
warn() { printf 'WARNING: %s\n' "$*" >&2; }
info() { printf '==> %s\n' "$*"; }
need() { command -v "$1" >/dev/null 2>&1 || die "required command not found: $1"; }

# --- repository paths ---
UPSTREAM_DIR="$PROJECT_ROOT/upstream"
PATCHES_DIR="$PROJECT_ROOT/patches"
PLATFORM_DIR="$PROJECT_ROOT/platform"
UPSTREAM_COMMIT_FILE="$PROJECT_ROOT/UPSTREAM_COMMIT"

BUILD_DIR="${BROGUE_BUILD_DIR:-$PROJECT_ROOT/build}"
WORK_DIR="$BUILD_DIR/work"
DIST_DIR="${BROGUE_DIST_DIR:-$PROJECT_ROOT/dist}"
BINARY_NAME="brogue"
BUILT_BINARY="$BUILD_DIR/$BINARY_NAME"

# --- SDK cache / reuse ---
SDK_TUPLE="arm-buildroot-linux-gnueabihf"
TARGET_PREFIX="${SDK_TUPLE}-"
CACHE_ROOT="${BROGUE_CACHE_ROOT:-${XDG_CACHE_HOME:-$HOME/.cache}/brogue-ce-rg35xx}"
SDK_CACHE_DIR="$CACHE_ROOT/sdk"
DOWNLOAD_CACHE_DIR="$CACHE_ROOT/downloads"
SDK_DIR="${BROGUE_SDK:-$SDK_CACHE_DIR}"

# --- pinned Koriki/Batocera SDK release ---
SDK_TAG="koriki_batocera_rg35xx_sdk_20240208"
SDK_ASSET="arm-buildroot-linux-gnueabihf_sdk-buildroot.tar.gz"
SDK_URL="https://github.com/rg35xx-cfw/Koriki/releases/download/${SDK_TAG}/${SDK_ASSET}"
SDK_SIZE="1231743769"
SDK_SHA256="1648ed2117df2974db48a967e1e53e1fd67cdbf06be47df384562496040391de"
EXPECTED_GCC="11.4.0"
DEVICE_GLIBC_MAX="2.37"

# --- upstream ---
UPSTREAM_URL="https://github.com/tmewett/BrogueCE.git"

pinned_commit() {
  awk 'NF && $1 !~ /^#/ {print $1; exit}' "$UPSTREAM_COMMIT_FILE"
}
