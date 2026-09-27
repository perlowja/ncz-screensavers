#!/bin/bash
# test-fix.sh — runs each rss-sdl2 hack with the fixed harness and
# extracts the framebuffer sample data.
set -u

pkill -9 -f _gles3 2>/dev/null
sleep 2
mkdir -p /tmp/fm/logs /tmp/fm/shots

cd /home/pegasus/ncz-screensavers/build

for hack in cyclone euphoria fieldlines flocks flux helios hyperspace implicitdemo lattice microcosm plasma skyrocket solarwinds; do
  log=/tmp/fm/logs/${hack}.err
  rm -f $log
  echo "=== running $hack ==="
  XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0 \
    nohup ./${hack}_gles3 >/dev/null 2>$log </dev/null &
  pid=$!
  start=$SECONDS
  while (( SECONDS - start < 25 )); do
    grep -q "framebuffer frame=4 " $log 2>/dev/null && break
    kill -0 $pid 2>/dev/null || break
    sleep 0.2
  done
  shot1=/tmp/fm/shots/${hack}-1.png
  XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0 grim $shot1 2>/dev/null
  start=$SECONDS
  while (( SECONDS - start < 15 )); do
    grep -q "framebuffer frame=60 " $log 2>/dev/null && break
    kill -0 $pid 2>/dev/null || break
    sleep 0.2
  done
  shot2=/tmp/fm/shots/${hack}-2.png
  XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0 grim $shot2 2>/dev/null
  kill -TERM $pid 2>/dev/null; sleep 1; kill -KILL $pid 2>/dev/null
  fb4=$(grep "framebuffer frame=4 " $log 2>/dev/null | head -1 | sed 's/.*nonblack=\([0-9]*\).*/\1/')
  fb60=$(grep "framebuffer frame=60 " $log 2>/dev/null | head -1 | sed 's/.*nonblack=\([0-9]*\).*/\1/')
  echo "$hack: fb4=$fb4 fb60=$fb60"
done
echo "=== DONE ==="