#!/bin/bash
export XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0
cd $HOME/ncz-screensavers/builddir
for t in fliptext bouncingcow cubenetic glblur menger timetunnel gltext flurry; do
  BIN=$(ls -d ./${t}*gles3 2>/dev/null | head -1)
  OUT=$HOME/remeasure/$t
  rm -rf $OUT; mkdir -p $OUT
  echo "=== $t $(date +%H:%M:%S) ==="
  NCZ_FRAME_DUMP=$OUT/ ncz-display-run -t 22 "$BIN" 2>&1 | grep -cE "framebuffer frame=" | sed "s/^/  diag_lines=/"
  echo "  pngs=$(ls $OUT 2>/dev/null | wc -l)"
done
echo REMEASURE_DONE
