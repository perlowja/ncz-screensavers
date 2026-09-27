#!/bin/bash
# validate-xshadertoy.sh — adapted for xshadertoy diag format.
# Captures two grim screenshots + reads xshadertoy [diag] frame=N samples.
# Note: xshadertoy emits frame=4, frame=60, frame=120, frame=180, ...
# The driver samples 4 points (centre, 3 corners) — useful but limited.

set -u

BIN_DIR="${BIN_DIR:-$HOME/xshadertoy-bin}"
RESULT_DIR="${RESULT_DIR:-$HOME/xshadertoy-scan-$(date +%F-%H%M%S)}"
SHUTDOWN_TIMEOUT="${SHUTDOWN_TIMEOUT:-5}"
FIRST_FRAME="${FIRST_FRAME:-60}"
SECOND_FRAME="${SECOND_FRAME:-180}"
HOST_TAG="${HOST_TAG:-host}"
CAPTURE_MODE="${CAPTURE_MODE:-grim}"

mkdir -p "$RESULT_DIR/shots" "$RESULT_DIR/logs"

# Set up Wayland
if [ -z "${XDG_RUNTIME_DIR:-}" ]; then
    export XDG_RUNTIME_DIR="/run/user/$(id -u)"
fi
if [ -z "${WAYLAND_DISPLAY:-}" ]; then
    export WAYLAND_DISPLAY="wayland-0"
fi
export NCZ_NO_LAYER_SHELL=1

printf 'target\tstatus\tfirst_samples\tsecond_samples\trenderer\tfailure\trc\n' > "$RESULT_DIR/results.tsv"
printf 'host=%s\nXDG_RUNTIME_DIR=%s\nWAYLAND_DISPLAY=%s\n' \
    "$(hostname)" "$XDG_RUNTIME_DIR" "$WAYLAND_DISPLAY" > "$RESULT_DIR/environment.txt"

wait_for_sample() {
    local log=$1 pid=$2 frame=$3 timeout=$4 start=$SECONDS
    while (( SECONDS - start < timeout )); do
        grep -q "xshadertoy frame=$frame " "$log" 2>/dev/null && return 0
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

mapfile -t bins < <(find "$BIN_DIR" -maxdepth 1 -type f -executable -name 'xshadertoy_*_gles3' -printf '%f\n' | sort)
echo "Running ${#bins[@]} targets on $(uname -m) via $XDG_RUNTIME_DIR/$WAYLAND_DISPLAY"
echo "RESULT_DIR=$RESULT_DIR"

for bin in "${bins[@]}"; do
    log="$RESULT_DIR/logs/$bin.stderr"; exitf="$RESULT_DIR/logs/$bin.exit"
    shot1="$RESULT_DIR/shots/$bin-1.png"; shot2="$RESULT_DIR/shots/$bin-2.png"

    (cd "$BIN_DIR" && "./$bin") >"$log" 2>&1 &
    pid=$!

    # Wait for the first diag line (frame=4) to confirm it initialised
    start=$SECONDS
    while (( SECONDS - start < 10 )); do
        grep -q "xshadertoy frame=4 " "$log" 2>/dev/null && break
        kill -0 "$pid" 2>/dev/null || break
        sleep 0.1
    done

    # Wait for first significant frame
    if wait_for_sample "$log" "$pid" "$FIRST_FRAME" 30; then
        if [ "$CAPTURE_MODE" = "grim" ]; then
            grim "$shot1" 2>>"$log" || true
        fi
        if wait_for_sample "$log" "$pid" "$SECOND_FRAME" 30; then
            if [ "$CAPTURE_MODE" = "grim" ]; then
                grim "$shot2" 2>>"$log" || true
            fi
        fi
    fi

    stop_run "$pid"; rc=$run_rc; echo "$rc" > "$exitf"

    renderer=$(grep -m1 'RENDERER=' "$log" 2>/dev/null | sed 's/.*RENDERER=//' | tr '\t' ' ')

    first_samples=$(grep "xshadertoy frame=$FIRST_FRAME " "$log" 2>/dev/null | tail -1 | sed 's/.*samples=//')
    second_samples=$(grep "xshadertoy frame=$SECOND_FRAME " "$log" 2>/dev/null | tail -1 | sed 's/.*samples=//')

    failure=-
    status=PASS
    if [ -n "$(grep -m1 'GL ERROR' "$log" 2>/dev/null)" ] || grep -q "shader compile failed\|program link failed" "$log"; then
        status=FAIL; failure=compile_error
    elif [ "$gate_killed" -eq 1 ]; then status=INCONCLUSIVE; failure=harness_shutdown_timeout
    elif [ "$rc" -eq 137 ]; then status=INCONCLUSIVE; failure=signal_9
    elif [ "$rc" -ne 0 ]; then status=FAIL; failure="exit_$rc"
    elif [ -z "$first_samples" ] && [ -z "$second_samples" ]; then
        status=FAIL; failure=no_diag
    fi

    printf '%s\t%s\t"%s"\t"%s"\t"%s"\t%s\t%s\n' \
        "$bin" "$status" "$first_samples" "$second_samples" "$renderer" "$failure" "$rc" >> "$RESULT_DIR/results.tsv"
    if [ "$failure" = - ]; then echo "$bin $status"; else echo "$bin $status ($failure)"; fi
done

echo "Done. RESULT_DIR=$RESULT_DIR"