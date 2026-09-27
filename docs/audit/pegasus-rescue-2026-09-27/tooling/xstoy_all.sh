#!/bin/bash
# xstoy_all.sh — drive ALL 38 xshadertoy captures from a single long
# SSH session, with no per-shader subprocess boundary. Designed to
# avoid SSH session re-init overhead.
set +e
out=/tmp/xstoy-evidence
mkdir -p "$out"
rm -f "$out"/*
export __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/10_nvidia.json
export __GLX_VENDOR_LIBRARY_NAME=nvidia
export __NV_PRIME_RENDER_OFFLOAD=1
export WAYLAND_DISPLAY=wayland-0
export XDG_RUNTIME_DIR=/run/user/$(id -u)
export NCZ_SHADER_DIR=/home/pegasus/Projects/ncz-screensavers/vendor/xshadertoy/glsl
cd /

shaders=$(ls /home/pegasus/Projects/ncz-screensavers/vendor/xshadertoy/glsl/*.glsl | xargs -n1 basename | sed 's/.glsl//' | sort)
total=$(echo "$shaders" | wc -l)
i=0
for name in $shaders; do
    i=$((i+1))
    pkill -9 -f "xshadertoy_${name}_gles3" 2>/dev/null
    sleep 0.5
    log="$out/${name}.log"
    : > "$log"
    /home/pegasus/Projects/ncz-screensavers/build/xshadertoy_${name}_gles3 > "$log" 2>&1 &
    PID=$!
    # wait for initial draw
    j=0
    while [ $j -lt 200 ] && ! grep -q "initial draw" "$log" 2>/dev/null && kill -0 $PID 2>/dev/null; do
        sleep 0.05
        j=$((j+1))
    done
    if ! grep -q "initial draw" "$log"; then
        echo "[$i/$total] $name: TIMEOUT"
        kill -9 $PID 2>/dev/null
        continue
    fi
    sleep 2
    grim "$out/${name}-2.png" 2>/dev/null
    sleep 8
    grim "$out/${name}-10.png" 2>/dev/null
    kill -9 $PID 2>/dev/null
    wait $PID 2>/dev/null
    renderer=$(grep "RENDERER=" "$log" | head -1 | sed 's/RENDERER=//')
    lasterr=$(grep -oE "gl_error=0x[0-9a-fA-F]+" "$log" | tail -1)
    nb=$(grep -oE "nonblack=[0-9]+" "$log" | tail -1)
    echo "[$i/$total] $name  renderer=$renderer  $lasterr  $nb"
    pkill -9 -f "xshadertoy_${name}_gles3" 2>/dev/null
done
echo DONE
