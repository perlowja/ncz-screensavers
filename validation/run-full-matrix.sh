#!/usr/bin/env bash
# Verdicts come from the hack framebuffer and stderr. Full-output screenshots
# are retained only as supporting artifacts, never as proof that the hack drew.
set -u

BIN_DIR="${BIN_DIR:-$HOME/gles3-validation/bin}"
RESULT_DIR="${RESULT_DIR:-$HOME/gles3-validation/full-matrix-$(date +%F-%H%M%S)}"
STARTUP_TIMEOUT="${STARTUP_TIMEOUT:-30}"
FRAME_TIMEOUT="${FRAME_TIMEOUT:-15}"
SHUTDOWN_TIMEOUT="${SHUTDOWN_TIMEOUT:-5}"
FIRST_FRAME="${FIRST_FRAME:-4}"
SECOND_FRAME="${SECOND_FRAME:-60}"
mkdir -p "$RESULT_DIR/shots" "$RESULT_DIR/logs"

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
. "$SCRIPT_DIR/full-matrix-lib.sh"
eval "$("$SCRIPT_DIR/find-wayland.sh")" || exit 2
export NCZ_NO_LAYER_SHELL=1
if [ "$(uname -m)" = aarch64 ]; then
    export NCZ_GPU_BACKEND=mali
    export __EGL_VENDOR_LIBRARY_FILENAMES=/opt/cixgpu-compat/share/glvnd/egl_vendor.d/40_cix.json:/usr/share/glvnd/egl_vendor.d/50_mesa.json
fi

printf 'target\tstatus\texit_code\tshot1_bytes\tshot2_bytes\tmean1\tmean2\tchanged_pixels\trenderer\tfailure\n' > "$RESULT_DIR/results.tsv"
printf 'arch=%s\nXDG_RUNTIME_DIR=%s\nWAYLAND_DISPLAY=%s\n' "$(uname -m)" "$XDG_RUNTIME_DIR" "$WAYLAND_DISPLAY" > "$RESULT_DIR/environment.txt"

wait_for_sample() {
    local log=$1 pid=$2 frame=$3 timeout=$4 start=$SECONDS
    while (( SECONDS - start < timeout )); do
        grep -q "^\[diag\] framebuffer frame=$frame " "$log" 2>/dev/null && return 0
        kill -0 "$pid" 2>/dev/null || return 1
        sleep 0.1
    done
    return 2
}

stop_run() {
    local pid=$1 waited=0
    gate_killed=0
    if kill -0 "$pid" 2>/dev/null; then
        kill -TERM "$pid" 2>/dev/null || true
        while kill -0 "$pid" 2>/dev/null && (( waited < SHUTDOWN_TIMEOUT * 10 )); do
            sleep 0.1
            waited=$((waited + 1))
        done
        if kill -0 "$pid" 2>/dev/null; then
            gate_killed=1
            kill -KILL "$pid" 2>/dev/null || true
        fi
    fi
    wait "$pid" 2>/dev/null
    run_rc=$?
}

mapfile -t bins < <(find "$BIN_DIR" -maxdepth 1 -type f -executable -name '*_gles3' -printf '%f\n' | sort)
echo "Running ${#bins[@]} targets on $(uname -m) via $XDG_RUNTIME_DIR/$WAYLAND_DISPLAY"
for bin in "${bins[@]}"; do
    log="$RESULT_DIR/logs/$bin.stderr"; exitf="$RESULT_DIR/logs/$bin.exit"
    shot1="$RESULT_DIR/shots/$bin-1.png"; shot2="$RESULT_DIR/shots/$bin-2.png"
    (cd "$BIN_DIR" && "./$bin") >"$log" 2>&1 &
    pid=$!

    progress_failure=-
    if wait_for_sample "$log" "$pid" "$FIRST_FRAME" "$STARTUP_TIMEOUT"; then
        grim "$shot1" 2>>"$log" || true
        if wait_for_sample "$log" "$pid" "$SECOND_FRAME" "$FRAME_TIMEOUT"; then
            grim "$shot2" 2>>"$log" || true
        else
            case $? in 1) progress_failure=exited_before_second_frame ;; *) progress_failure=frame_timeout ;; esac
        fi
    else
        case $? in 1) progress_failure=exited_before_first_frame ;; *) progress_failure=startup_timeout ;; esac
    fi

    stop_run "$pid"; rc=$run_rc; echo "$rc" > "$exitf"
    b1=$(stat -c%s "$shot1" 2>/dev/null || echo 0); b2=$(stat -c%s "$shot2" 2>/dev/null || echo 0)
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
            changed=${changed%% *}; case "$changed" in ''|*[!0-9]*) changed=0 ;; esac
        fi
    fi

    renderer=$(grep -m1 'RENDERER=' "$log" 2>/dev/null | sed 's/.*RENDERER=//' | tr '\t' ' ')
    fatal=$(matrix_fatal_line "$log")
    sample1=$(matrix_framebuffer_sample "$log" "$FIRST_FRAME")
    sample2=$(matrix_framebuffer_sample "$log" "$SECOND_FRAME")
    read -r nonblack1 hash1 error1 <<<"${sample1:-0 - missing}"
    read -r nonblack2 hash2 error2 <<<"${sample2:-0 - missing}"

    failure=-
    if [ -n "$fatal" ]; then status=FAIL; failure=$(matrix_tsv_field "$fatal")
    elif [ "$gate_killed" -eq 1 ]; then status=INCONCLUSIVE; failure=harness_shutdown_timeout
    elif [ "$rc" -eq 137 ]; then status=INCONCLUSIVE; failure=signal_9
    elif [ "$progress_failure" != - ]; then status=INCONCLUSIVE; failure=$progress_failure
    elif [ "$rc" -ne 0 ]; then status=FAIL; failure="exit_$rc"
    elif [ "$error1" != 0x0 ] || [ "$error2" != 0x0 ]; then status=FAIL; failure="framebuffer_gl_error_${error1}_${error2}"
    elif [ "$nonblack1" -eq 0 ] && [ "$nonblack2" -eq 0 ]; then status=FAIL; failure=black_framebuffer
    elif [ "$hash1" = "$hash2" ]; then status=FAIL; failure=static_framebuffer
    else status=PASS
    fi
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$bin" "$status" "$rc" "$b1" "$b2" "$mean1" "$mean2" "$changed" "$renderer" "$failure" >> "$RESULT_DIR/results.tsv"
    if [ "$failure" = - ]; then echo "$bin $status"; else echo "$bin $status ($failure)"; fi
done
