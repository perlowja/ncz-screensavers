#!/bin/bash
# xstoy_batch.sh — capture a batch of shaders, APPENDING to existing
# evidence (no rm -rf). Skips shaders whose .log already exists.
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
for name in "$@"; do
    # skip if already done
    if [ -s "$out/${name}-10.png" ]; then
        echo "$name: already done"
        continue
    fi
    # safer kill — avoid matching own bash
    for p in $(pgrep -f "xshadertoy_${name}_gles3" 2>/dev/null); do
        kill -9 "$p" 2>/dev/null
    done
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
    grim "$png2" 2>/dev/null
    sleep 8
    grim "$png10" 2>/dev/null
    kill -9 $PID 2>/dev/null
    wait $PID 2>/dev/null
    for p in $(pgrep -f "xshadertoy_${name}_gles3" 2>/dev/null); do
        kill -9 "$p" 2>/dev/null
    done
    renderer=$(grep "RENDERER=" "$log" | head -1 | sed 's/RENDERER=//')
    lasterr=$(grep -oE "gl_error=0x[0-9a-fA-F]+" "$log" | tail -1)
    nb=$(grep -oE "nonblack=[0-9]+" "$log" | tail -1)
    echo "$name  ${renderer:0:50}  ${lasterr}  ${nb}"
done
