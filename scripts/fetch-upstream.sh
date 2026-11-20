#!/usr/bin/env bash
# Re-clone the pinned upstream Brogue CE commit and replace the vendored upstream/ snapshot.
# Optional maintenance tool; requires network. Does not touch anything outside this repository.
set -u
SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib.sh"
need git

FORCE=0
for arg in "$@"; do [ "$arg" = "--force" ] && FORCE=1; done

COMMIT="$(pinned_commit)"
[ -n "$COMMIT" ] || die "cannot read pinned commit from $UPSTREAM_COMMIT_FILE"

manifest() { # stable checksum of a tree excluding .git
  (cd "$1" && find . -path './.git' -prune -o -type f -print0 | sort -z | xargs -0 sha256sum | sha256sum | awk '{print $1}')
}

TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/brogue-upstream.XXXXXX")" || die "mktemp failed"
trap 'rm -rf "$TMP_DIR"' EXIT

info "Cloning $UPSTREAM_URL"
git clone --quiet "$UPSTREAM_URL" "$TMP_DIR/repo" || die "git clone failed"
git -C "$TMP_DIR/repo" checkout --quiet --detach "$COMMIT" || die "pinned commit not found: $COMMIT"
ACTUAL="$(git -C "$TMP_DIR/repo" rev-parse HEAD)"
[ "$ACTUAL" = "$COMMIT" ] || die "fetched HEAD $ACTUAL != pinned $COMMIT"

if [ -d "$UPSTREAM_DIR" ] && [ "$FORCE" -ne 1 ]; then
  OLD_MANIFEST="$(manifest "$UPSTREAM_DIR")"
  NEW_MANIFEST="$(manifest "$TMP_DIR/repo")"
  if [ "$OLD_MANIFEST" != "$NEW_MANIFEST" ]; then
    die "local upstream/ differs from the pinned clone; re-run with --force to replace it"
  fi
fi

rm -rf "$UPSTREAM_DIR"
mkdir -p "$UPSTREAM_DIR" || die "cannot create $UPSTREAM_DIR"
cp -a "$TMP_DIR/repo/." "$UPSTREAM_DIR/" || die "copy failed"
rm -rf "$UPSTREAM_DIR/.git"

info "upstream/ refreshed at pinned commit $COMMIT"
