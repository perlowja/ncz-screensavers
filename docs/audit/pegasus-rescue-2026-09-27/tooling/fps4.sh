#!/bin/bash
export XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0
cd $HOME/ncz-screensavers/builddir
printf "%-12s %14s %14s\n" target iGPU_frames_10s RTX_frames_10s
for t in cellmosaic hexlattice ridgeline wellcurve blackhole; do
  B=$(ls -d ./xshadertoy_${t}*gles3 2>/dev/null | head -1); [ -z "$B" ] && B=$(ls -d ./${t}*gles3 2>/dev/null | head -1)
  [ -x "$B" ] || { printf "%-12s %14s\n" $t MISSING; continue; }
  i=$(ncz-display-run -t 10 "$B" 2>&1 | grep -oE "frame=[0-9]+" | tail -1 | cut -d= -f2)
  n=$(__NV_PRIME_RENDER_OFFLOAD=1 ncz-display-run -t 10 "$B" 2>&1 | grep -oE "frame=[0-9]+" | tail -1 | cut -d= -f2)
  printf "%-12s %14s %14s\n" "$t" "${i:-0}" "${n:-0}"
done
echo FPS4_DONE
