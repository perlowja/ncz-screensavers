#!/bin/bash
export XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0
cd $HOME/ncz-screensavers/builddir
OUT=$HOME/verify4; rm -rf $OUT; mkdir -p $OUT
for t in cellmosaic hexlattice ridgeline wellcurve; do
  for run in 1 2; do
    D=$OUT/${t}-run${run}; mkdir -p $D
    echo "=== $t run$run $(date +%H:%M:%S) ==="
    NCZ_FRAME_DUMP=$D/ ncz-display-run -t 14 ./xshadertoy_${t}*gles3 2>&1 | grep -E "iSeed|framebuffer frame=(120|240)" | head -3
  done
done
echo VERIFY4_DONE
