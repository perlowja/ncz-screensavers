#!/bin/bash
# capture-magma-r19.sh — comprehensive capture run for round 19:
#   - 4+ colourways (not the safest ones)
#   - side-by-side vs round-18 at the same seed
#   - a merge-in-progress and a necking-during-separation
#   - frame-time data for all launches
#
# Run on Pegasus (Intel UHD). Assumes magmasimplex_gles3 has been
# built with the round-19 shader and src.

set -u

repo="$HOME/ncz-screensavers"
bin="$repo/build-magmasimplex/magmasimplex_gles3"
out="$HOME/build-tmp/magma-r19-final"
mkdir -p "$out"

if [ -x "$repo/validation/find-wayland.sh" ]; then
    eval "$("$repo/validation/find-wayland.sh")"
    export XDG_RUNTIME_DIR WAYLAND_DISPLAY
fi

cd "$repo"
test -x "$bin" || { echo "ERR: binary missing"; exit 1; }

# 4 colourways — chosen to show the range, not the safest:
#   seed=25  way=12  ultraviolet (red liquid + rainbow blobs)
#   seed=60  way=29  neon (magenta liquid + mint wax)
#   seed=70  way=7   blue + yellow wax
#   seed=80  way=18  orange / purple
# Plus we add seed=27 (way=13, red/yellow) to catch a different
# pair, and seed=33 (way=17, purple/red) for a 6th colourway.

seeds=(25 27 33 60 70 80)

for seed in "${seeds[@]}"; do
  run="$out/seed-$seed"
  mkdir -p "$run"
  log="$run/run.log"
  rm -f "$log"

  # Capture at t=3, 6, 10, 14s with PERF_LOG on.
  NCZ_MAGMASIMPLEX_FIXED_SEED=$seed NCZ_MAGMASIMPLEX_PERF_LOG=1 \
    "$bin" >"$log" 2>&1 &
  pid=$!
  sleep 3
  grim "$run/t03.png" 2>>"$log" || echo "grim t03 FAIL" >>"$log"
  sleep 3
  grim "$run/t06.png" 2>>"$log" || echo "grim t06 FAIL" >>"$log"
  sleep 4
  grim "$run/t10.png" 2>>"$log" || echo "grim t10 FAIL" >>"$log"
  sleep 4
  grim "$run/t14.png" 2>>"$log" || echo "grim t14 FAIL" >>"$log"
  kill -TERM "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true
  sleep 1

  # Capture seed/diag line
  grep -m1 "magmasimplex seed=" "$log" | head -c 300 > "$run/seed.txt" 2>/dev/null
  echo >> "$run/seed.txt"

  # Perf log lines
  grep "frame_t" "$log" > "$run/perf.log" 2>/dev/null
done

ls -la "$out" | head