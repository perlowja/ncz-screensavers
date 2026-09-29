#!/bin/sh
# runs ON O6N: fps vs render scale for the shader hacks. Usage: o6sweep.sh
LOCKSH=$HOME/host-lock.sh
sh $LOCKSH acquire test lead1 "fps vs render-scale sweep of shader hacks (0.4.0)" --expect 75 --wait ${LOCKWAIT:-900} || exit 9
cleanup() { pkill -f "/a40/run/" 2>/dev/null; sh $LOCKSH release; }
trap cleanup EXIT INT TERM
cd $HOME/a40/run || exit 1
for v in $(cat /proc/$(pgrep -u $(id -u) -o labwc)/environ | tr '\0' '\n' | grep -E '^(__EGL|VK_|LD_LIBRARY|LIBGL|MESA|GBM|EGL_|GALLIUM|PAN|MALI|CIX)' | sed 's/ /%20/g'); do export "$(echo $v | sed 's/%20/ /g')"; done
export XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0 NCZ_SHADER_DIR=$HOME/a40/usr/share/ncz-screensavers/shaders
[ -r /proc/cmdline ] && echo "cmdline: $(grep -o 'module_blacklist=[^ ]*' /proc/cmdline)" > sweep.txt
echo "modules: $(lsmod | grep -oE 'mali_kbase|panthor' | tr '\n' ' ')" >> sweep.txt
echo "output: $(wlr-randr | grep current | head -1)" >> sweep.txt
run() { # bin scale
  out=$(timeout -k 2 12 nice -n 5 ./$1 --render-scale=$2 --render-scale-mode=fixed 2>&1 | grep '^\[stats\]' | tail -1)
  echo "$1 $2 $out"
}
last=0
hacks=$(ls | grep _gles3 | grep -E '^(blackhole|hyprsaver_|xshadertoy_)')
for h in $hacks; do
  run $h 1 >> sweep.txt
  last=$((last+1)); [ $((last % 10)) -eq 0 ] && sh $LOCKSH refresh
done
# second phase: slow hacks at lower scales
for h in $hacks; do
  fps=$(grep "^$h 1 " sweep.txt | sed -n 's/.*steady n=\([0-9]*\).*/\1/p')
  [ -z "$fps" ] && fps=0
  if [ "$fps" -lt 260 ]; then   # under ~52 fps over the ~5 s steady window
    for s in 0.75 0.5 0.35; do run $h $s >> sweep.txt; done
    sh $LOCKSH refresh
  fi
done
echo DONE >> sweep.txt
