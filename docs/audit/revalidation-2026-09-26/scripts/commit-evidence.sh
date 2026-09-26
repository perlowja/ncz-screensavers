#!/bin/bash
# Copy analyzed evidence into the repo's docs/audit/revalidation-2026-09-26/
# Run after analyze.py has processed each capture dir.
# Usage: commit-evidence.sh <gpu-tag>
set -u

GPU="${1:-i}"
SRC_BASE="$HOME/build-tmp/audit-2026-09-26/pegasus-${GPU}"
REPO="$HOME/Projects/ncz-screensavers"
DEST_BASE="$REPO/docs/audit/revalidation-2026-09-26/pegasus-${GPU}"

mkdir -p "$DEST_BASE"

# Iterate all evidence dirs
find "$SRC_BASE" -type d -mindepth 2 -maxdepth 2 | while read -r DIR; do
    REL=$(realpath --relative-to="$SRC_BASE" "$DIR")  # e.g. xshadertoy/cyclone_gles3
    DEST="$DEST_BASE/$REL"
    mkdir -p "$DEST"
    # Copy non-huge evidence files
    for f in metrics.json log.txt exit.txt renderer.txt hung.txt framebuffer.log nframes.txt; do
        [ -f "$DIR/$f" ] && cp "$DIR/$f" "$DEST/$f"
    done
    # Copy small downscaled PNGs (keep all)
    for f in "$DIR"/small_*.png; do
        [ -f "$f" ] && cp "$f" "$DEST/$(basename "$f")"
    done
    # Keep at least one full-res PNG per target
    fulls=($(ls "$DIR"/frame_*.png 2>/dev/null | grep -v small | head -3))
    for f in "${fulls[@]}"; do
        [ -f "$f" ] && cp "$f" "$DEST/fullres_$(basename "$f")"
    done
    # Note for non-GOOD captures
    echo "$REL: $(ls $DEST | wc -l) files" >&2
done

echo "Evidence staged in $DEST_BASE"
