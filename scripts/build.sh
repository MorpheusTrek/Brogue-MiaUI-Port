#!/usr/bin/env bash
# Cross-build Brogue CE with the pinned Koriki/Batocera SDK.
# Default: stock SDL backend.  --handheld (or BROGUE_HANDHELD=1): RG35XX backend.
set -u
SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib.sh"

HANDHELD="${BROGUE_HANDHELD:-0}"
DRY_RUN=0
for arg in "$@"; do
  case "$arg" in
    --dry-run) DRY_RUN=1 ;;
    --handheld) HANDHELD=1 ;;
    *) die "unknown argument: $arg" ;;
  esac
done
case "$HANDHELD" in
  0|1) ;;
  *) die "BROGUE_HANDHELD must be 0 or 1 (got: $HANDHELD)" ;;
esac

if [ "$DRY_RUN" -eq 1 ]; then
  DRY_SDK="${BROGUE_SDK:-$SDK_CACHE_DIR}"
  PASS_SYSROOT="${BROGUE_PASS_SYSROOT:-0}"
  SYS_CFLAGS=""; SYS_LDFLAGS=""
  if [ "$PASS_SYSROOT" = "1" ]; then
    SYS_CFLAGS="--sysroot=$DRY_SDK/$SDK_TUPLE/sysroot"
    SYS_LDFLAGS="--sysroot=$DRY_SDK/$SDK_TUPLE/sysroot"
  fi
  printf '%s\n' "(dry run) SDK:                 $DRY_SDK"
  printf '%s\n' "(dry run) backend:             $([ "$HANDHELD" = 1 ] && echo handheld || echo stock)"
  printf '%s\n' "(dry run) build tree:          $WORK_DIR (prepared from $UPSTREAM_DIR by apply-patches.sh)"
  printf '%s\n' "(dry run) cross compiler:      $DRY_SDK/bin/${TARGET_PREFIX}gcc"
  printf '%s\n' "(dry run) sdl2-config wrapper: $PROJECT_ROOT/scripts/sdl2-config"
  printf '%s\n' "(dry run) command (run in $WORK_DIR):"
  cat <<EOF
make -j${JOBS:-2} \\
  CC="$DRY_SDK/bin/${TARGET_PREFIX}gcc" \\
  SYSTEM=LINUXLIKE GRAPHICS=YES TERMINAL=NO WEBBROGUE=NO \\
  BROGUE_HANDHELD=$HANDHELD \\
  RAPIDBROGUE=NO MAC_APP=NO RELEASE=YES DEBUG=NO \\
  DATADIR=. SDL_CONFIG="$PROJECT_ROOT/scripts/sdl2-config" \\
  CFLAGS="$SYS_CFLAGS \${EXTRA_CFLAGS:-}" \\
  LDFLAGS="$SYS_LDFLAGS -L$DRY_SDK/$SDK_TUPLE/sysroot/usr/lib \${EXTRA_LDFLAGS:-}" \\
  bin/brogue
EOF
  exit 0
fi

source "$SCRIPT_DIR/koriki-env.sh"
need make

info "Verifying SDK"
bash "$SCRIPT_DIR/verify-sdk.sh" --quiet "$SDK" || die "SDK verification failed"

bash "$SCRIPT_DIR/apply-patches.sh" || die "failed to prepare build tree"

PASS_SYSROOT="${BROGUE_PASS_SYSROOT:-0}"
SYS_CFLAGS=""; SYS_LDFLAGS=""
if [ "$PASS_SYSROOT" = "1" ]; then
  SYS_CFLAGS="--sysroot=$SYSROOT"
  SYS_LDFLAGS="--sysroot=$SYSROOT"
fi
CFLAGS_VALUE="$SYS_CFLAGS ${EXTRA_CFLAGS:-}"
LDFLAGS_VALUE="$SYS_LDFLAGS -L$SYSROOT/usr/lib ${EXTRA_LDFLAGS:-}"
JOBS="${JOBS:-2}"

info "Cross-compiling Brogue CE (backend=$([ "$HANDHELD" = 1 ] && echo handheld || echo stock), jobs=$JOBS)"
(
  cd "$WORK_DIR" || exit 1
  make -j"$JOBS" \
    CC="$CC" CXX="$CXX" \
    SYSTEM=LINUXLIKE GRAPHICS=YES TERMINAL=NO WEBBROGUE=NO \
    BROGUE_HANDHELD="$HANDHELD" \
    RAPIDBROGUE=NO MAC_APP=NO RELEASE=YES DEBUG=NO \
    DATADIR=. SDL_CONFIG="$SDL_CONFIG" \
    CFLAGS="$CFLAGS_VALUE" LDFLAGS="$LDFLAGS_VALUE" \
    bin/brogue
) || die "cross-build failed"

mkdir -p "$BUILD_DIR" || die "cannot create $BUILD_DIR"
cp "$WORK_DIR/bin/brogue" "$BUILT_BINARY" || die "cannot copy built binary"
chmod 755 "$BUILT_BINARY"
printf '%s\n' "$HANDHELD" > "$BUILD_DIR/build-mode.txt"
info "Built: $BUILT_BINARY (backend=$([ "$HANDHELD" = 1 ] && echo handheld || echo stock))"
info "Next: ./scripts/validate.sh && ./scripts/package.sh"
