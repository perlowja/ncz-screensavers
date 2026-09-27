#!/bin/bash
export XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0
cd $HOME/ncz-screensavers/builddir
OUT=$HOME/rss-visual-evidence; mkdir -p $OUT
for t in euphoria skyrocket solarwinds cyclone flocks; do
  BIN=$(ls -d ./${t}*gles3 2>/dev/null | head -1)
  [ -x "$BIN" ] || { echo "$t: NO BINARY"; continue; }
  echo "=== $t $(date +%H:%M:%S) $BIN ==="
  NCZ_FRAME_DUMP=$OUT/$t/ ncz-display-run -t 45 "$BIN" 2>&1 | grep -E 'frame=(240|420)|exit=' | tail -2
done
echo RSS_VISUAL_DONE
