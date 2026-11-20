#!/usr/bin/env bash
# Assemble a MiaUI .pak from a built stock or handheld Brogue CE binary. No device access.
set -u
SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib.sh"

TARBALL=0
BIN_ARG=""
for arg in "$@"; do
  case "$arg" in
    --tarball) TARBALL=1 ;;
    *) BIN_ARG="$arg" ;;
  esac
done
BIN="${BIN_ARG:-$BUILT_BINARY}"
[ -f "$BIN" ] || die "built binary not found: $BIN (run scripts/build.sh first)"

PAK_NAME="brogue-ce-rg35xx.pak"
PAK="$DIST_DIR/$PAK_NAME"
info "Packaging $BIN -> $PAK"

rm -rf "$PAK"
mkdir -p "$PAK/assets" "$PAK/saves" "$DIST_DIR" || die "cannot create package directories"

cp "$BIN" "$PAK/brogue" || die "cannot copy binary"
chmod 755 "$PAK/brogue"

for asset in tiles.png tiles.bin icon.png; do
  [ -f "$UPSTREAM_DIR/bin/assets/$asset" ] || die "missing upstream asset: $asset"
  cp "$UPSTREAM_DIR/bin/assets/$asset" "$PAK/assets/$asset" || die "cannot copy $asset"
done
[ -f "$UPSTREAM_DIR/bin/assets/LICENSE.txt" ] || die "missing tiles license notice"
cp "$UPSTREAM_DIR/bin/assets/LICENSE.txt" "$PAK/assets/LICENSE.txt"

# Stage 3 handheld UI font (DejaVu Sans Mono, permissive license).
for asset in DejaVuSansMono.ttf LICENSE-DejaVu.txt; do
  [ -f "$PROJECT_ROOT/assets/$asset" ] || die "missing UI asset: $asset"
  cp "$PROJECT_ROOT/assets/$asset" "$PAK/assets/$asset" || die "cannot copy $asset"
done

# Bundle the SDL2_ttf runtime so the pak never installs or modifies a system
# library. On-device system availability is [U]; see NOTICE.md / stage3 spec.
TTF_LIB="$SDK_DIR/$SDK_TUPLE/sysroot/usr/lib/libSDL2_ttf-2.0.so.0"
if [ -f "$TTF_LIB" ]; then
  mkdir -p "$PAK/lib"
  cp "$TTF_LIB" "$PAK/lib/" || die "cannot bundle SDL2_ttf runtime"
else
  warn "SDL2_ttf runtime not found at $TTF_LIB; pak will rely on a device copy [U]"
fi
cp "$UPSTREAM_DIR/bin/keymap.txt" "$PAK/keymap.txt" || die "cannot copy keymap.txt"
cp "$PROJECT_ROOT/LICENSE.txt" "$PAK/LICENSE.txt" || die "cannot copy AGPL license"
cp "$UPSTREAM_DIR/external/cJSON/LICENSE" "$PAK/cJSON_LICENSE.txt" || die "cannot copy cJSON license"

cat > "$PAK/launch.sh" <<'EOF'
#!/bin/sh
# Brogue CE (RG35XX) launcher. Keeps all CWD-relative game data inside the pak.
GAMEDIR="$(cd "$(dirname "$0")" >/dev/null 2>&1 && pwd)"
cd "$GAMEDIR" || exit 1

##############################
## CPU: MinUI game speed (medium, ~1.2 GHz)
##############################
# Saving replays the whole input recording on one thread; the menu clock (504
# MHz) makes a long load take ~26 s. Use the reversible pattern from the other
# local ports (Undertale, Downwell) and restore the stock setting on exit.
overclock.elf "$CPU_SPEED_GAME" 2>/dev/null
for i in 0 1 2 3; do
    echo performance > "/sys/devices/system/cpu/cpu$i/cpufreq/scaling_governor" 2>/dev/null
done
restore_cpu() {
    for i in 0 1 2 3; do
        echo userspace > "/sys/devices/system/cpu/cpu$i/cpufreq/scaling_governor" 2>/dev/null
        echo 504000 > "/sys/devices/system/cpu/cpu$i/cpufreq/scaling_setspeed" 2>/dev/null
    done
}
trap restore_cpu EXIT INT TERM

# Prefer pak-local runtime libraries (bundled SDL2_ttf) over system copies.
if [ -d "$GAMEDIR/lib" ]; then
    LD_LIBRARY_PATH="$GAMEDIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    export LD_LIBRARY_PATH
fi

# Optional pak-local SDL controller mapping, used by the Stage 2 handheld backend.
if [ -f "$GAMEDIR/gamecontroller.txt" ]; then
    SDL_GAMECONTROLLERCONFIG="$(cat "$GAMEDIR/gamecontroller.txt")"
    export SDL_GAMECONTROLLERCONFIG
fi

exec "$GAMEDIR/brogue" "$@"
EOF
chmod 755 "$PAK/launch.sh"

cat > "$PAK/gamecontroller.txt.example" <<'EOF'
# Example SDL_GAMECONTROLLERCONFIG entry.
# Rename to gamecontroller.txt to have launch.sh export it.
# Obtain the RG35XX mapping from /userdata/Profile/configs/gamecontrollerdb.txt on the device.
# Obtain the real mapping on-device; the physical mapping is unverified [U].
# Optional diagnostics: set BROGUE_HANDHELD_DEBUG_INPUT=1 in the launch environment.
EOF

if [ "$TARBALL" -eq 1 ]; then
  TAR="$DIST_DIR/$PAK_NAME.tar.gz"
  tar -czf "$TAR" -C "$DIST_DIR" "$PAK_NAME" || die "tarball creation failed"
  info "Tarball: $TAR"
fi

info "Package contents:"
find "$PAK" -maxdepth 2 -printf '%M %P\n' | sort
info "PACKAGE READY: $PAK (deployment requires separate approval)"
