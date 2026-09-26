#!/bin/sh
# Sweep all 13 rss-sdl2 GLES3 targets on PEGASUS, capture stderr + frame,
# and pull frame_0000003c.rgba locally for visual inspection.
#
# Usage: ./sweep_rss_sdl2.sh <output_dir>
#   output_dir  local directory to receive per-target subdirs
#
# The capture is the same as the prior 2026-09-26 sweep in
# docs/audit/revalidation-2026-09-26/ but with the new gles3_compat
# fix (commit cf59926) which impls glColorMaterial and routes live
# state through display-list replay.

set -e
OUTDIR="${1:-$(dirname "$0")/pegasus-i}"
PEGASUS=pegasus@192.168.207.85
SSH="sshpass -p pegasus ssh -o PubkeyAuthentication=no -o IdentityAgent=none \
  -o IdentitiesOnly=yes -o PreferredAuthentications=password \
  -o StrictHostKeyChecking=no -o ConnectTimeout=10 -o NumberOfPasswordPrompts=1 \
  -o UserKnownHostsFile=$HOME/build-tmp/pegasus-known-hosts"
HACKS="cyclone euphoria fieldlines flocks flux helios hyperspace \
       implicitdemo lattice microcosm plasma skyrocket solarwinds"

mkdir -p "$OUTDIR"
for h in $HACKS; do
    echo "=== $h ==="
    rm -rf "$OUTDIR/$h"
    mkdir -p "$OUTDIR/$h"
    # On PEGASUS, capture stderr + frames
    sshpass -p pegasus ssh -o PubkeyAuthentication=no -o IdentityAgent=none \
        -o IdentitiesOnly=yes -o PreferredAuthentications=password \
        -o StrictHostKeyChecking=no -o ConnectTimeout=10 -o NumberOfPasswordPrompts=1 \
        -o UserKnownHostsFile=$HOME/build-tmp/pegasus-known-hosts \
        pegasus@192.168.207.85 "rm -rf ~/build-tmp/cap-$h; \
        mkdir -p ~/build-tmp/cap-$h; \
        cd ~/ncz-screensavers && \
        NCZ_FRAME_DUMP=/home/pegasus/build-tmp/cap-$h \
        timeout 8 ./build/${h}_gles3 2> /tmp/run-$h.stderr > /dev/null; \
        echo EXIT=\$? >> /tmp/run-$h.stderr; \
        ls -la ~/build-tmp/cap-$h/"
    # Pull stderr
    sshpass -p pegasus scp -o PubkeyAuthentication=no -o IdentityAgent=none \
        -o IdentitiesOnly=yes -o PreferredAuthentications=password \
        -o StrictHostKeyChecking=no -o ConnectTimeout=10 -o NumberOfPasswordPrompts=1 \
        -o UserKnownHostsFile=$HOME/build-tmp/pegasus-known-hosts \
        pegasus@192.168.207.85:/tmp/run-$h.stderr "$OUTDIR/$h/run.stderr"
    # Pull frames 60, 240, 600
    for hex in 3c f0 258; do
        sshpass -p pegasus scp -o PubkeyAuthentication=no -o IdentityAgent=none \
            -o IdentitiesOnly=yes -o PreferredAuthentications=password \
            -o StrictHostKeyChecking=no -o ConnectTimeout=10 -o NumberOfPasswordPrompts=1 \
            -o UserKnownHostsFile=$HOME/build-tmp/pegasus-known-hosts \
            pegasus@192.168.207.85:/home/pegasus/build-tmp/cap-$h/frame_000000$hex.rgba \
            "$OUTDIR/$h/frame_${hex}.rgba" 2>/dev/null || echo "MISSING frame $hex"
    done
done
echo "ALL DONE"
