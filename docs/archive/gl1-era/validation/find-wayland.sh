#!/usr/bin/env bash
# Locate a Wayland compositor that accepts real client connections.
#
# Usage:
#   eval "$(validation/find-wayland.sh)"
#
# An explicitly supplied XDG_RUNTIME_DIR/WAYLAND_DISPLAY pair is tried first.
# Otherwise every Wayland socket below /run/user is probed.  Merely finding a
# socket is not sufficient: the selected compositor must complete a Wayland
# roundtrip through wlr-randr or wayland-info.

set -u

probe_wayland() {
    local runtime_dir="$1"
    local display="$2"
    if command -v wlr-randr >/dev/null 2>&1; then
        XDG_RUNTIME_DIR="$runtime_dir" WAYLAND_DISPLAY="$display" \
            timeout 3 wlr-randr >/dev/null 2>&1
    elif command -v wayland-info >/dev/null 2>&1; then
        XDG_RUNTIME_DIR="$runtime_dir" WAYLAND_DISPLAY="$display" \
            timeout 3 wayland-info >/dev/null 2>&1
    else
        echo "find-wayland: need wlr-randr or wayland-info to verify a compositor" >&2
        return 2
    fi
}

emit_wayland() {
    printf 'export XDG_RUNTIME_DIR=%q\nexport WAYLAND_DISPLAY=%q\n' "$1" "$2"
}

if [ -n "${XDG_RUNTIME_DIR:-}" ] && [ -n "${WAYLAND_DISPLAY:-}" ] \
        && [ -S "$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY" ] \
        && probe_wayland "$XDG_RUNTIME_DIR" "$WAYLAND_DISPLAY"; then
    emit_wayland "$XDG_RUNTIME_DIR" "$WAYLAND_DISPLAY"
    exit 0
fi

while IFS= read -r socket; do
    runtime_dir=${socket%/*}
    display=${socket##*/}
    if probe_wayland "$runtime_dir" "$display"; then
        emit_wayland "$runtime_dir" "$display"
        exit 0
    fi
done < <(find /run/user -mindepth 2 -maxdepth 2 -type s -name 'wayland-*' -print 2>/dev/null | sort)

echo "find-wayland: no live Wayland compositor found under /run/user/*/wayland-*" >&2
exit 1
