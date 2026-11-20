#!/usr/bin/env bash
# Prepare a clean build tree: upstream/ -> build/work/, apply patches/, copy platform/ sources.
set -u
SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib.sh"
need cp
need rm

[ -d "$UPSTREAM_DIR" ] || die "missing upstream source tree: $UPSTREAM_DIR"

info "Preparing build tree at $WORK_DIR"
rm -rf "$WORK_DIR"
mkdir -p "$BUILD_DIR" "$WORK_DIR" || die "cannot create build directories"
cp -a "$UPSTREAM_DIR/." "$WORK_DIR/" || die "cannot copy upstream source"
rm -rf "$WORK_DIR/.git"

shopt -s nullglob
patches=("$PATCHES_DIR"/*.patch)
shopt -u nullglob
if [ "${#patches[@]}" -gt 0 ]; then
  for patch_file in "${patches[@]}"; do
    info "Applying patch: $(basename "$patch_file")"
    if command -v patch >/dev/null 2>&1; then
      (cd "$WORK_DIR" && patch -p1 --forward < "$patch_file") || die "patch failed: $patch_file"
    else
      (cd "$WORK_DIR" && git apply --unsafe-paths "$patch_file") || die "patch failed: $patch_file"
    fi
  done
else
  info "No patches found (Stage 1: pristine upstream)"
fi

shopt -s nullglob
plat_sources=("$PLATFORM_DIR"/*.c "$PLATFORM_DIR"/*.h "$PLATFORM_DIR"/*.mk)
shopt -u nullglob
if [ "${#plat_sources[@]}" -gt 0 ]; then
  for src in "${plat_sources[@]}"; do
    info "Adding platform source: $(basename "$src")"
    cp -a "$src" "$WORK_DIR/src/platform/" || die "cannot copy $src"
  done
else
  info "No platform sources found (Stage 1: no handheld backend)"
fi

info "Build tree ready"
