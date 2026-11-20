#!/usr/bin/env bash
# Static ABI check + emulated seed-catalog smoke test. No device access.
set -u
SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib.sh"

SKIP_QEMU=0
for arg in "$@"; do [ "$arg" = "--no-qemu" ] && SKIP_QEMU=1; done

[ -f "$BUILT_BINARY" ] || die "built binary not found: $BUILT_BINARY (run scripts/build.sh first)"

info "=== static ABI verification ==="
bash "$SCRIPT_DIR/check-binary.sh" "$BUILT_BINARY" native || die "binary ABI check failed"

if [ "$SKIP_QEMU" -eq 1 ]; then
  info "--no-qemu: skipping emulated smoke test"
  exit 0
fi

if [ ! -d "$SDK_DIR" ]; then
  warn "SDK not found at $SDK_DIR; skipping emulated smoke test"
  exit 0
fi
source "$SCRIPT_DIR/koriki-env.sh"

QEMU="$SDK/bin/qemu-arm"
if [ ! -x "$QEMU" ]; then
  warn "qemu-arm not found in SDK; skipping emulated smoke test"
  exit 0
fi

EXPECTED="$UPSTREAM_DIR/test/seed_catalogs/seed_catalog_brogue.txt"
[ -f "$EXPECTED" ] || die "missing seed catalog baseline: $EXPECTED"

RUN_DIR="$BUILD_DIR/validate-run"
mkdir -p "$RUN_DIR" || die "cannot create $RUN_DIR"
ACTUAL="$RUN_DIR/seed_catalog_actual.txt"

info "=== emulated seed-catalog smoke test (max level 40) ==="
(
  cd "$RUN_DIR" || exit 1
  SDL_VIDEODRIVER=dummy "$QEMU" -L "$SYSROOT" "$BUILT_BINARY" \
    --print-seed-catalog 1 25 40 > "$ACTUAL"
) || die "emulated Brogue run failed"

if diff <(sed '1,4d' "$EXPECTED") <(sed '1,4d' "$ACTUAL"); then
  info "Seed catalog identical to pinned baseline: PASS"
else
  die "seed catalog differs from pinned baseline"
fi

info "VALIDATION PASSED (emulation only; no on-device runtime validation performed)"
