#!/usr/bin/env bash
# Reuse BROGUE_SDK or download/verify/extract the pinned SDK into the user-level cache.
# Never writes to the repository, /etc, or global shell configuration.
set -u
SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib.sh"

info "SDK target: $SDK_DIR"

if [ -n "${BROGUE_SDK:-}" ]; then
  info "BROGUE_SDK is set; reusing existing SDK (no download)"
  exec bash "$SCRIPT_DIR/verify-sdk.sh" "$SDK_DIR"
fi

if [ -d "$SDK_CACHE_DIR" ] && bash "$SCRIPT_DIR/verify-sdk.sh" --quiet "$SDK_CACHE_DIR" >/dev/null 2>&1; then
  info "Cached SDK already verified: $SDK_CACHE_DIR"
  exit 0
fi

need curl
need tar
need gzip
need sha256sum
mkdir -p "$DOWNLOAD_CACHE_DIR" || die "cannot create cache directory: $DOWNLOAD_CACHE_DIR"

ARCHIVE="$DOWNLOAD_CACHE_DIR/$SDK_ASSET"
if [ ! -f "$ARCHIVE" ]; then
  info "Downloading SDK to $ARCHIVE"
  curl -fL --retry 3 -o "$ARCHIVE" "$SDK_URL" || die "download failed: $SDK_URL"
else
  info "Resuming/verifying cached archive: $ARCHIVE"
  curl -fL --retry 3 -C - -o "$ARCHIVE" "$SDK_URL" || \
    warn "resume not supported or download reported an error; verifying existing file"
fi

actual_size="$(stat -c%s "$ARCHIVE" 2>/dev/null || echo 0)"
if [ "$actual_size" != "$SDK_SIZE" ]; then
  die "archive size $actual_size != expected $SDK_SIZE; remove $ARCHIVE and retry"
fi
info "Archive size OK ($actual_size bytes)"

printf '%s  %s\n' "$SDK_SHA256" "$ARCHIVE" | sha256sum -c - || die "SHA-256 mismatch for $ARCHIVE"
info "SHA-256 OK (local integrity pin)"

gzip -t "$ARCHIVE" || die "gzip integrity check failed"
info "gzip integrity OK"

TMP_SDK="${SDK_CACHE_DIR}.tmp.$$"
rm -rf "$TMP_SDK"
mkdir -p "$TMP_SDK" || die "cannot create temp SDK dir"
info "Extracting SDK (this can take a while)"
tar --strip-components=1 -xzf "$ARCHIVE" -C "$TMP_SDK" || die "extraction failed"

if [ -x "$TMP_SDK/relocate-sdk.sh" ]; then
  info "Running SDK relocate script"
  (cd "$TMP_SDK" && ./relocate-sdk.sh) || die "relocate-sdk.sh failed"
fi

bash "$SCRIPT_DIR/verify-sdk.sh" --quiet "$TMP_SDK" || die "extracted SDK failed verification"

if [ -e "$SDK_CACHE_DIR" ]; then
  OLD_SDK="${SDK_CACHE_DIR}.old.$$"
  mv "$SDK_CACHE_DIR" "$OLD_SDK" || die "cannot move aside old SDK"
  rm -rf "$OLD_SDK"
fi
mv "$TMP_SDK" "$SDK_CACHE_DIR" || die "cannot move SDK into place"

info "SDK installed and verified at $SDK_CACHE_DIR"
