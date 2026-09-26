#!/bin/bash
set -u
for TGT in xshadertoy_alienbeacon_gles3 cyclone_gles3 plasma_gles3; do
    echo "=== $TGT ==="
    TARGET_DIR="/tmp/sanity/$TGT"
    rm -rf "$TARGET_DIR"
    ~/build-tmp/audit-2026-09-26/scripts/capture-target.sh i "$TGT" "$TARGET_DIR" 6 2>&1 | tail -2
    echo "  exit=$(cat "$TARGET_DIR/exit.txt") frames=$(ls "$TARGET_DIR"/frame_*.png 2>/dev/null | wc -l)"
    cat "$TARGET_DIR/renderer.txt" 2>/dev/null
    cat "$TARGET_DIR/framebuffer.log" 2>/dev/null | head -3
done
