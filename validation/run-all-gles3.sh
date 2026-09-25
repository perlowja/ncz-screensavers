#!/bin/bash
# run-all-gles3.sh — runs every _gles3 binary in $BIN_DIR for a few seconds,
# captures grim screenshots per binary, logs stderr from each binary,
# records exit status, and saves everything to a timestamped results dir.
#
# Designed to run ON the test host (O6N, MEDUSA, PEGASUS). The deploy
# script (deploy-to-remote.sh) populates $BIN_DIR (default:
# ~/gles3-validation/bin/) before this runs.
#
# Usage:
#   BIN_DIR=~/gles3-validation/bin SHOT_DIR=~/gles3-validation/shots \
#     LOG_DIR=~/gles3-validation/logs ./run-all-gles3.sh
#
# Output:
#   $SHOT_DIR/<hack>_gles3.png      — grim screenshot
#   $LOG_DIR/<hack>_gles3.stderr    — stderr from the binary
#   $LOG_DIR/<hack>_gles3.exit      — exit code (or "killed" if SIGTERM)
#   $RESULTS_CSV                    — one row per binary:
#                                      hack, exit_code, screenshot_bytes,
#                                      stderr_size, status (PASS/BLACK/CRASH/
#                                      HANG/skip), diag_gl_renderer
#
# Environment variables:
#   BIN_DIR            — dir containing *_gles3 binaries (default: current dir)
#   SHOT_DIR           — where to write grim screenshots (default: ./shots)
#   LOG_DIR            — where to write per-binary logs (default: ./logs)
#   RESULTS_CSV        — output CSV file (default: ./results.csv)
#   RUN_SECONDS        — seconds to wait before grim capture (default: 2)
#   POST_SECONDS       — seconds to wait AFTER capture before SIGTERM
#                        (default: 1)
#   SIGKILL_GRACE      — SIGKILL fallback after this many seconds past SIGTERM
#                        (default: 3)
#   SKIP_BASELINE      — set to 1 to skip the baseline (empty-screen) capture
#   DRY_RUN            — set to 1 to print what would be run without executing
#
# This script does NOT itself classify results as PASS/FAIL/BLACK/CRASH/HANG —
# it records raw evidence (screenshot, stderr, exit code). The downstream
# docs/CROSS-PLATFORM-GLES3-VALIDATION-*.md writes PASS/BLACK/etc. based on
# the screenshot and stderr contents.

set -u

BIN_DIR="${BIN_DIR:-.}"
SHOT_DIR="${SHOT_DIR:-./shots}"
LOG_DIR="${LOG_DIR:-./logs}"
RESULTS_CSV="${RESULTS_CSV:-./results.csv}"
RUN_SECONDS="${RUN_SECONDS:-2}"
POST_SECONDS="${POST_SECONDS:-1}"
SIGKILL_GRACE="${SIGKILL_GRACE:-3}"
SKIP_BASELINE="${SKIP_BASELINE:-0}"
DRY_RUN="${DRY_RUN:-0}"

mkdir -p "$SHOT_DIR" "$LOG_DIR"

# Find binary list
cd "$BIN_DIR"
mapfile -t BINARIES < <(ls -1 *_gles3 2>/dev/null | sort)

if [ ${#BINARIES[@]} -eq 0 ]; then
    echo "FATAL: no *_gles3 binaries found in $BIN_DIR" >&2
    exit 2
fi

echo "Found ${#BINARIES[@]} binaries in $BIN_DIR"

# Check for grim
if ! command -v grim >/dev/null 2>&1; then
    echo "FATAL: grim not installed" >&2
    exit 2
fi

# Select a socket by completing a real Wayland roundtrip.  This also finds a
# greeter compositor owned by a non-login UID and non-default wayland-N names.
FIND_WAYLAND="$(cd "$(dirname "$0")" && pwd)/find-wayland.sh"
if ! WAYLAND_ENV=$("$FIND_WAYLAND"); then
    echo "FATAL: no live Wayland compositor found" >&2
    exit 2
fi
eval "$WAYLAND_ENV"

echo "WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-<unset>}"
echo "XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-<unset>}"
echo "WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-<unset>}" > "$LOG_DIR/_env.txt"
env >> "$LOG_DIR/_env.txt"

# On NCZ-OS hosts (O6N/Sky1 in particular), the Wayland compositor
# (labwc) is configured to fall back to Mesa swrast (CPU) for any app
# not in the CIX vendor's allowlist. The real Panthor/Mali-G720 GPU
# is only reached when the app sets the CIX EGL vendor library and
# NCZ_GPU_BACKEND=mali. Without this, we get llvmpipe which is not
# representative of the actual GPU the system ships with — and on
# ARM64 boxen where llvmpipe is the only renderer, every hack would
# "work" trivially on swrast even if it was broken on real GPU GLES.
#
# We set both for all platforms; MEDUSA/PEGASUS simply ignore them
# (their EGL ICDs are picked up by glvnd from /usr/share/glvnd
# regardless of __EGL_VENDOR_LIBRARY_FILENAMES contents).
export NCZ_GPU_BACKEND="${NCZ_GPU_BACKEND:-mali}"
export __EGL_VENDOR_LIBRARY_FILENAMES="${__EGL_VENDOR_LIBRARY_FILENAMES:-/opt/cixgpu-compat/share/glvnd/egl_vendor.d/40_cix.json:/usr/share/glvnd/egl_vendor.d/50_mesa.json}"
echo "NCZ_GPU_BACKEND=$NCZ_GPU_BACKEND" >> "$LOG_DIR/_env.txt"
echo "__EGL_VENDOR_LIBRARY_FILENAMES=$__EGL_VENDOR_LIBRARY_FILENAMES" >> "$LOG_DIR/_env.txt"

# Baseline screenshot
if [ "$SKIP_BASELINE" != "1" ]; then
    echo "Capturing baseline screenshot..."
    grim "$SHOT_DIR/_baseline.png" 2>"$LOG_DIR/_baseline.stderr" || true
    if [ -s "$SHOT_DIR/_baseline.png" ]; then
        BASELINE_BYTES=$(stat -c%s "$SHOT_DIR/_baseline.png")
        echo "Baseline: $BASELINE_BYTES bytes"
    else
        echo "WARNING: baseline screenshot is empty/zero bytes"
        BASELINE_BYTES=0
    fi
fi

# CSV header
echo "binary,exit_code,screenshot_bytes,stderr_bytes,wallclock_sec,diag_gl_renderer" > "$RESULTS_CSV"

run_one() {
    local bin="$1"
    local base="${bin%_gles3}"
    local shot="$SHOT_DIR/${bin}.png"
    local log="$LOG_DIR/${bin}.stderr"
    local exitf="$LOG_DIR/${bin}.exit"
    local start=$(date +%s.%N)

    if [ "$DRY_RUN" = "1" ]; then
        echo "[DRY] $bin"
        return
    fi

    # Launch in background, capture stderr
    "./$bin" > "$log" 2>&1 &
    local pid=$!

    # Wait RUN_SECONDS
    sleep "$RUN_SECONDS"

    # Capture screenshot
    grim "$shot" 2>>"$log" || true

    # Wait POST_SECONDS
    sleep "$POST_SECONDS"

    # Send SIGTERM
    kill -TERM "$pid" 2>/dev/null || true

    # Wait up to SIGKILL_GRACE for clean exit
    local waited=0
    while kill -0 "$pid" 2>/dev/null; do
        sleep 0.1
        waited=$((waited + 1))
        if [ $waited -ge $((SIGKILL_GRACE * 10)) ]; then
            kill -KILL "$pid" 2>/dev/null || true
            break
        fi
    done

    wait "$pid" 2>/dev/null
    local rc=$?
    if [ $waited -ge $((SIGKILL_GRACE * 10)) ]; then
        rc=137  # SIGKILL
    elif [ $rc -eq 143 ]; then
        rc="killed_SIGTERM"
    fi

    local end=$(date +%s.%N)
    local elapsed=$(echo "$end - $start" | bc -l)
    local shot_bytes=$(stat -c%s "$shot" 2>/dev/null || echo 0)
    local log_bytes=$(stat -c%s "$log" 2>/dev/null || echo 0)
    local renderer=$(grep -E "RENDERER=" "$log" | head -1 | sed 's/.*RENDERER=//' | tr -d '\n')

    echo "$rc" > "$exitf"
    echo "$bin,$rc,$shot_bytes,$log_bytes,$elapsed,$renderer" >> "$RESULTS_CSV"
}

# Run all binaries (sequentially — each one grabs Wayland output, so
# concurrent runs would fight over the screen)
for bin in "${BINARIES[@]}"; do
    echo -n "$bin ... "
    run_one "$bin"
    echo "done"
done

echo ""
echo "=== Summary ==="
echo "Total: $(wc -l < "$RESULTS_CSV") - 1"
echo "Output: $RESULTS_CSV"
echo "Screenshots: $SHOT_DIR"
echo "Logs: $LOG_DIR"
