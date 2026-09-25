#!/usr/bin/env bash
# Run every staged *_gles3 binary and capture two real frames per target.
# Classification requires a successful sustained run, visible pixels, and
# meaningful pixel changes between captures.

set -u

BIN_DIR="${BIN_DIR:-$HOME/gles3-validation/bin}"
RESULT_DIR="${RESULT_DIR:-$HOME/gles3-validation/full-matrix-2026-09-25}"
FIRST_SECONDS="${FIRST_SECONDS:-2}"
SECOND_SECONDS="${SECOND_SECONDS:-2}"
mkdir -p "$RESULT_DIR/shots" "$RESULT_DIR/logs"

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
eval "$("$SCRIPT_DIR/find-wayland.sh")" || exit 2
export NCZ_NO_LAYER_SHELL=1

if [ "$(uname -m)" = aarch64 ]; then
    export NCZ_GPU_BACKEND=mali
    export __EGL_VENDOR_LIBRARY_FILENAMES=/opt/cixgpu-compat/share/glvnd/egl_vendor.d/40_cix.json:/usr/share/glvnd/egl_vendor.d/50_mesa.json
fi

printf 'target\tstatus\texit_code\tshot1_bytes\tshot2_bytes\tmean1\tmean2\tchanged_pixels\trenderer\tfailure\n' > "$RESULT_DIR/results.tsv"
printf 'arch=%s\nXDG_RUNTIME_DIR=%s\nWAYLAND_DISPLAY=%s\n' "$(uname -m)" "$XDG_RUNTIME_DIR" "$WAYLAND_DISPLAY" > "$RESULT_DIR/environment.txt"

mapfile -t bins < <(find "$BIN_DIR" -maxdepth 1 -type f -executable -name '*_gles3' -printf '%f\n' | sort)
echo "Running ${#bins[@]} targets on $(uname -m) via $XDG_RUNTIME_DIR/$WAYLAND_DISPLAY"

for bin in "${bins[@]}"; do
    log="$RESULT_DIR/logs/$bin.stderr"
    exitf="$RESULT_DIR/logs/$bin.exit"
    shot1="$RESULT_DIR/shots/$bin-1.png"
    shot2="$RESULT_DIR/shots/$bin-2.png"
    (cd "$BIN_DIR" && "./$bin") >"$log" 2>&1 &
    pid=$!
    sleep "$FIRST_SECONDS"
    grim "$shot1" 2>>"$log" || true
    sleep "$SECOND_SECONDS"
    grim "$shot2" 2>>"$log" || true
    kill -TERM "$pid" 2>/dev/null || true
    for _ in $(seq 1 30); do kill -0 "$pid" 2>/dev/null || break; sleep 0.1; done
    if kill -0 "$pid" 2>/dev/null; then kill -KILL "$pid" 2>/dev/null || true; fi
    wait "$pid" 2>/dev/null
    rc=$?
    echo "$rc" > "$exitf"

    b1=$(stat -c%s "$shot1" 2>/dev/null || echo 0)
    b2=$(stat -c%s "$shot2" 2>/dev/null || echo 0)
    mean1=0; mean2=0; changed=0
    if [ "$b1" -gt 0 ] && [ "$b2" -gt 0 ]; then
        if python3 -c 'import PIL,numpy' >/dev/null 2>&1; then
            read -r mean1 mean2 changed < <(python3 - "$shot1" "$shot2" <<'PY'
import sys
import numpy as np
from PIL import Image
a = np.asarray(Image.open(sys.argv[1]).convert("RGB"))
b = np.asarray(Image.open(sys.argv[2]).convert("RGB"))
print(float(a.mean()) / 255, float(b.mean()) / 255,
      int(np.any(a != b, axis=2).sum()))
PY
            )
        else
            mean1=$(magick "$shot1" -colorspace RGB -format '%[fx:mean]' info: 2>/dev/null || echo 0)
            mean2=$(magick "$shot2" -colorspace RGB -format '%[fx:mean]' info: 2>/dev/null || echo 0)
            changed=$(magick compare -metric AE "$shot1" "$shot2" null: 2>&1 || true)
            changed=${changed%% *}
            case "$changed" in ''|*[!0-9]*) changed=0 ;; esac
        fi
    fi
    renderer=$(grep -m1 'RENDERER=' "$log" 2>/dev/null | sed 's/.*RENDERER=//' | tr '\t' ' ')
    failure="-"
    if grep -q 'wl_display_connect failed' "$log"; then status=FAIL; failure=wl_display_connect
    elif [ "$rc" != 143 ] && [ "$rc" != 0 ]; then status=FAIL; failure="exit_$rc"
    elif [ "$b1" -eq 0 ] || [ "$b2" -eq 0 ]; then status=FAIL; failure=no_capture
    elif awk "BEGIN { exit !(($mean1 < 0.001) && ($mean2 < 0.001)) }"; then status=FAIL; failure=black
    elif [ "$changed" -le 1000 ]; then status=FAIL; failure=static
    else status=PASS
    fi
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$bin" "$status" "$rc" "$b1" "$b2" "$mean1" "$mean2" "$changed" "$renderer" "$failure" >> "$RESULT_DIR/results.tsv"
    if [ "$failure" = "-" ]; then echo "$bin $status"; else echo "$bin $status ($failure)"; fi
done
