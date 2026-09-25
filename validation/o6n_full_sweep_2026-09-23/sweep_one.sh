#!/bin/bash
# sweep_one.sh — launch one hack on real O6N hardware, capture screenshot, classify.
#
# Usage:
#   sweep_one.sh <binary_basename> [run_seconds]
#
# Side effects (on agent host, ULTRA):
#   - Reads sshpass credentials
#   - Writes screenshot, stderr, exit_code to:
#       validation/o6n_full_sweep_2026-09-23/screenshots/<bin>.png
#       validation/o6n_full_sweep_2026-09-23/logs/<bin>.stderr
#       validation/o6n_full_sweep_2026-09-23/logs/<bin>.exit
#       validation/o6n_full_sweep_2026-09-23/logs/<bin>.classify.json
#   - Emits one JSON line on stdout (compact; suitable for parsing):
#       {"bin":"...","exit_code":N,"screenshot_bytes":N,"stderr_bytes":N,
#        "pure_black_pct":N.NN,"distinct_colors":N,"status":"PASS|BLACK|CRASH|HANG|TIMEOUT|GL_FAIL",
#        "renderer":"..."}

set -u

BIN="$1"
RUN_SECONDS="${2:-3}"
POST_SECONDS="${POST_SECONDS:-1}"
SIGKILL_GRACE="${SIGKILL_GRACE:-3}"

SWEEP_DIR="$HOME/Projects/ncz-screensavers/validation/o6n_full_sweep_2026-09-23"
SHOT="$SWEEP_DIR/screenshots/${BIN}.png"
LOG="$SWEEP_DIR/logs/${BIN}.stderr"
EXITF="$SWEEP_DIR/logs/${BIN}.exit"
JSON="$SWEEP_DIR/logs/${BIN}.classify.json"

O6N_SSH="sshpass -p 'mini' ssh -o PubkeyAuthentication=no -o IdentitiesOnly=yes -o StrictHostKeyChecking=no mini@192.168.207.3"

# Ensure no leftover process for this binary on O6N
$O6N_SSH "pkill -f '/gles3-validation/bin/${BIN}\$'" 2>/dev/null || true

# Run remotely: launch binary, sleep, grim, kill, return base64 screenshot + stderr + exit
REMOTE_CMD=$(cat <<EOF
set -u
BIN_DIR=/home/mini/gles3-validation/bin
SHOT_REM=/tmp/sweep_shot_${BIN}.png
LOG_REM=/tmp/sweep_log_${BIN}.txt
export __EGL_VENDOR_LIBRARY_FILENAMES=/opt/cixgpu-compat/share/glvnd/egl_vendor.d/40_cix.json:/usr/share/glvnd/egl_vendor.d/50_mesa.json
export NCZ_GPU_BACKEND=mali
export LD_LIBRARY_PATH=/opt/singularity/lib/aarch64-linux-gnu:/opt/singularity/lib:/opt/cixgpu-pro/lib/aarch64-linux-gnu:/opt/cixgpu-compat/lib/aarch64-linux-gnu:/usr/lib/aarch64-linux-gnu
# Run
"\$BIN_DIR/$BIN" > "\$LOG_REM" 2>&1 &
PID=\$!
sleep $RUN_SECONDS
# Capture screenshot
WAYLAND_DISPLAY=wayland-0 XDG_RUNTIME_DIR=/run/user/1000 grim "\$SHOT_REM" 2>>"\$LOG_REM" || true
sleep $POST_SECONDS
# SIGTERM
kill -TERM \$PID 2>/dev/null || true
WAITED=0
while kill -0 \$PID 2>/dev/null; do
    sleep 0.1
    WAITED=\$((WAITED + 1))
    if [ \$WAITED -ge 30 ]; then
        kill -KILL \$PID 2>/dev/null || true
        break
    fi
done
wait \$PID 2>/dev/null
RC=\$?
if [ \$WAITED -ge 30 ]; then
    RC=137
elif [ \$RC -eq 143 ]; then
    RC=143
fi
SHOT_BYTES=\$(stat -c%s "\$SHOT_REM" 2>/dev/null || echo 0)
LOG_BYTES=\$(stat -c%s "\$LOG_REM" 2>/dev/null || echo 0)
echo "RC=\$RC"
echo "SHOT_BYTES=\$SHOT_BYTES"
echo "LOG_BYTES=\$LOG_BYTES"
if [ "\$SHOT_BYTES" -gt 0 ]; then
    base64 -w 0 "\$SHOT_REM" > /tmp/sweep_shot_${BIN}.b64
fi
if [ "\$LOG_BYTES" -gt 0 ]; then
    cat "\$LOG_REM" > /tmp/sweep_log_${BIN}.txt
fi
echo "DONE"
EOF
)

# Run the remote sequence, capturing output
RESULT=$(timeout 30 $O6N_SSH "$REMOTE_CMD" 2>&1)

# Pull the screenshot and log
RC=$(echo "$RESULT" | grep -E '^RC=' | head -1 | sed 's/^RC=//')
SHOT_REM_BYTES=$(echo "$RESULT" | grep -E '^SHOT_BYTES=' | head -1 | sed 's/^SHOT_BYTES=//')
LOG_REM_BYTES=$(echo "$RESULT" | grep -E '^LOG_BYTES=' | head -1 | sed 's/^LOG_BYTES=//')

# Defaults
RC="${RC:-137}"
SHOT_REM_BYTES="${SHOT_REM_BYTES:-0}"
LOG_REM_BYTES="${LOG_REM_BYTES:-0}"

# Pull the files
$O6N_SSH "cat /tmp/sweep_shot_${BIN}.b64 2>/dev/null | base64 -d > /tmp/__local_shot.png; cat /tmp/sweep_log_${BIN}.txt 2>/dev/null > /tmp/__local_log.txt; stat -c%s /tmp/__local_shot.png 2>/dev/null" 2>&1 > /tmp/__local_shot_stat.txt
LOCAL_SHOT_BYTES=$(cat /tmp/__local_shot_stat.txt)

if [ "${LOCAL_SHOT_BYTES:-0}" -gt 0 ] 2>/dev/null; then
    # Re-fetch directly
    $O6N_SSH "cat /tmp/__local_shot.png" > "$SHOT" 2>/dev/null
fi

# Pull log
$O6N_SSH "cat /tmp/sweep_log_${BIN}.txt 2>/dev/null" > "$LOG" 2>/dev/null

# Set exit code file
echo "$RC" > "$EXITF"

# Clean up remote
$O6N_SSH "rm -f /tmp/sweep_shot_${BIN}.png /tmp/sweep_shot_${BIN}.b64 /tmp/sweep_log_${BIN}.txt /tmp/__local_shot.png" 2>/dev/null

# Classify the screenshot if it exists
if [ -s "$SHOT" ]; then
    CLASSIFY_JSON=$(python3 -c "
import sys, json
from PIL import Image
import numpy as np
img = Image.open('$SHOT').convert('RGB')
arr = np.asarray(img)
h, w, _ = arr.shape
total = h * w
# Pure-black pixel count (R=G=B=0)
is_black = (arr.sum(axis=2) == 0)
pure_black_pct = 100.0 * is_black.sum() / total
# Distinct colors (downsample for speed)
# Use a quantized subset: take every 4th pixel in both dims
sub = arr[::4, ::4].reshape(-1, 3)
# Quantize to 5 bits per channel for speed (~32K buckets max)
qsub = (sub >> 3).astype(np.int32)
colors_q = np.unique(qsub[:,0]*1024 + qsub[:,1]*32 + qsub[:,2])
distinct_colors = int(len(colors_q))
# Renderer (from log)
print(json.dumps({
  'bin': '$BIN',
  'screenshot_bytes': $(stat -c%s "$SHOT" 2>/dev/null || echo 0),
  'stderr_bytes': $(stat -c%s "$LOG" 2>/dev/null || echo 0),
  'exit_code': int('$RC'),
  'pure_black_pct': round(float(pure_black_pct), 4),
  'distinct_colors': distinct_colors,
  'resolution': f'{w}x{h}'
}))
" 2>&1)
    echo "$CLASSIFY_JSON" > "$JSON"
    # Determine status
    EXIT_CODE_INT=$(echo "$CLASSIFY_JSON" | python3 -c "import sys,json; print(json.loads(sys.stdin.read())['exit_code'])")
    PB=$(echo "$CLASSIFY_JSON" | python3 -c "import sys,json; print(json.loads(sys.stdin.read())['pure_black_pct'])")
    DC=$(echo "$CLASSIFY_JSON" | python3 -c "import sys,json; print(json.loads(sys.stdin.read())['distinct_colors'])")
    RENDERER=$(grep -E '^RENDERER=' "$LOG" 2>/dev/null | head -1 | sed 's/.*RENDERER=//' | tr -d '\n')
    if [ "$EXIT_CODE_INT" = "0" ]; then
        # 0 means it ran to completion (which our harness stops via SIGTERM, so rc=143 usually).
        # Adjust: 143 = SIGTERM = killed by us = normal. So actually treat 143 as PASS for this context.
        STATUS="PASS"
    elif [ "$EXIT_CODE_INT" = "143" ]; then
        STATUS="PASS"
    elif [ "$EXIT_CODE_INT" = "137" ]; then
        STATUS="HANG"
    elif [ "$EXIT_CODE_INT" -lt 0 ] || [ "$EXIT_CODE_INT" -gt 128 ]; then
        STATUS="CRASH"
    else
        STATUS="GL_FAIL"
    fi
    # Override with black-detection logic: if it's pure-black (>99.5%) and very few colors, mark BLACK
    IS_BLACK=$(python3 -c "print('yes' if float('$PB') > 99.5 and int('$DC') < 50 else 'no')")
    if [ "$IS_BLACK" = "yes" ] && [ "$STATUS" = "PASS" ]; then
        STATUS="BLACK"
    fi
    echo "{\"bin\":\"$BIN\",\"exit_code\":$EXIT_CODE_INT,\"screenshot_bytes\":$(stat -c%s "$SHOT"),\"stderr_bytes\":$(stat -c%s "$LOG"),\"pure_black_pct\":$PB,\"distinct_colors\":$DC,\"status\":\"$STATUS\",\"renderer\":\"$RENDERER\"}"
else
    echo "{\"bin\":\"$BIN\",\"exit_code\":$RC,\"screenshot_bytes\":0,\"stderr_bytes\":$(stat -c%s "$LOG" 2>/dev/null || echo 0),\"pure_black_pct\":-1,\"distinct_colors\":-1,\"status\":\"NO_SHOT\",\"renderer\":\"\"}"
fi
