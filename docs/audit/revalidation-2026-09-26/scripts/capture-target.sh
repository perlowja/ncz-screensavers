#!/bin/bash
# Run one target on PEGASUS, capturing all frames.
# Usage: capture-target.sh <gpu-tag> <target-bin> <local-capture-dir> [timeout-seconds]
#
# gpu-tag: 'i' (Intel iGPU) or 'n' (NVIDIA RTX 2060)
# local-capture-dir: local path on THIS host; contents copied from remote.
set -u

GPU="${1:-i}"
TARGET="${2:-cyclone_gles3}"
LOCAL="${3:-/tmp/cap}"
TIMEOUT_S="${4:-30}"

REMOTE_CDIR="/tmp/cap-$$-$GPU-$TARGET"
SSH='timeout 60 sshpass -p pegasus ssh -o PubkeyAuthentication=no -o IdentityAgent=none -o IdentitiesOnly=yes -o PreferredAuthentications=password -o NumberOfPasswordPrompts=1 -o StrictHostKeyChecking=no pegasus@192.168.207.85'

if [ "$GPU" = "n" ]; then
    ENV_PREFIX='__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/10_nvidia.json __GLX_VENDOR_LIBRARY_NAME=nvidia __NV_PRIME_RENDER_OFFLOAD=1'
else
    ENV_PREFIX=''
fi

mkdir -p "$LOCAL"

# Clean and create remote dir
$SSH "rm -rf $REMOTE_CDIR && mkdir -p $REMOTE_CDIR" >/dev/null 2>&1

# Run target with hard timeout
CMD="cd ~/ncz-screensavers/builddir && ${ENV_PREFIX} NCZ_FRAME_DUMP=$REMOTE_CDIR timeout --kill-after=2 $TIMEOUT_S ./$TARGET; echo EXITCODE=\$?"
START=$(date +%s)
LOG=$($SSH "$CMD" 2>&1)
END=$(date +%s)
ELAPSED=$((END-START))

# Pull artifacts back
$SSH "cd $REMOTE_CDIR && tar cf - . " 2>/dev/null | (cd "$LOCAL" && tar xf -)

echo "$LOG" > "$LOCAL/log.txt"
EXIT=$(echo "$LOG" | grep -oE 'EXITCODE=[0-9]+' | tail -1 | cut -d= -f2)
echo "${EXIT:-NA}" > "$LOCAL/exit.txt"
echo "$LOG" | grep '^RENDERER=' > "$LOCAL/renderer.txt" || echo "RENDERER=?" > "$LOCAL/renderer.txt"
echo "$LOG" | grep "framebuffer frame=" > "$LOCAL/framebuffer.log" || true

NFRAMES=$(ls "$LOCAL"/frame_*.png 2>/dev/null | wc -l)
echo "$NFRAMES" > "$LOCAL/nframes.txt"

# HUNG detection
if [ "$ELAPSED" -ge "$((TIMEOUT_S-1))" ]; then
    echo "HUNG_OR_NEAR_TIMEOUT" > "$LOCAL/hung.txt"
else
    echo "OK" > "$LOCAL/hung.txt"
fi

echo "[$GPU $TARGET] exit=$EXIT nframes=$NFRAMES elapsed=${ELAPSED}s"
