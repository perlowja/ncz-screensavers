#!/bin/bash
# xstoy_clean.sh — like xstoy_batch but aggressively kills any blackhole/
# mapscroller overlay BEFORE each shader capture so grim captures our
# shader, not the other agent's overlay.
set +e
out=$2; shift 2
mkdir -p "$out"
export __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/10_nvidia.json
export __GLX_VENDOR_LIBRARY_NAME=nvidia
export __NV_PRIME_RENDER_OFFLOAD=1
export WAYLAND_DISPLAY=wayland-0
export XDG_RUNTIME_DIR=/run/user/$(id -u)
export NCZ_SHADER_DIR=/home/pegasus/Projects/ncz-screensavers/vendor/xshadertoy/glsl
cd /
kill_overlays() {
    for pat in blackhole mapscroller tunnel_; do
        for p in $(pgrep -f "$pat" 2>/dev/null); do
            [ "$p" = "$$" ] && continue
            kill -9 "$p" 2>/dev/null
        done
    done
}
for name in "$@"; do
    if [ -s "$out/${name}-10.png" ]; then
        echo "$name: already done"
        continue
    fi
    kill_overlays
    sleep 0.3
    log="$out/${name}.log"
    png2="$out/${name}-2.png"
    png10="$out/${name}-10.png"
    /home/pegasus/Projects/ncz-screensavers/build/xshadertoy_${name}_gles3 > "$log" 2>&1 &
    PID=$!
    j=0
    while [ $j -lt 200 ] && ! grep -q "initial draw" "$log" 2>/dev/null && kill -0 $PID 2>/dev/null; do
        sleep 0.05
        j=$((j+1))
    done
    if ! grep -q "initial draw" "$log"; then
        echo "$name: TIMEOUT"
        kill -9 $PID 2>/dev/null
        continue
    fi
    sleep 2
    kill_overlays
    grim "$png2" 2>/dev/null
    sleep 8
    kill_overlays
    grim "$png10" 2>/dev/null
    kill -9 $PID 2>/dev/null
    wait $PID 2>/dev/null
    kill_overlays
    renderer=$(grep "RENDERER=" "$log" | head -1 | sed 's/RENDERER=//')
    lasterr=$(grep -oE "gl_error=0x[0-9a-fA-F]+" "$log" | tail -1)
    nb=$(grep -oE "nonblack=[0-9]+" "$log" | tail -1)
    echo "$name  ${renderer:0:50}  ${lasterr}  ${nb}"
done
