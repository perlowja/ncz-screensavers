#!/bin/bash
# Run all _gles3 binaries on the test host against the live labwc session.
# Reads WAYLAND_DISPLAY / XDG_RUNTIME_DIR from env if set, otherwise defaults
# to /run/user/$(id -u) + wayland-0 (labwc's standard socket name).
#
# Usage (called from local validation/runner):
#   ./run-on-host.sh <platform-tag>
#
# platform-tag is one of: o6n | medusa | pegasus
# Used to name the output dir under validation/<platform>/raw/

set -u
platform="${1:-host}"
SRC_DIR="${HOME}/gles3-validation/src"
RESULTS_ROOT="${HOME}/gles3-validation"
RAW_DIR="${RESULTS_ROOT}/${platform}_raw"

mkdir -p "${RAW_DIR}/shots" "${RAW_DIR}/logs"

if ! WAYLAND_ENV=$("${SRC_DIR}/validation/find-wayland.sh"); then
    echo "FATAL: no live Wayland compositor found" >&2
    exit 2
fi
eval "$WAYLAND_ENV"
# Pin GLES3 path (not strictly needed on amd64 — glvnd picks up the
# active vendor ICD automatically — but keeps the diagnostic line in
# stderr consistent across platforms for diff'ing).
export NCZ_GPU_BACKEND="${NCZ_GPU_BACKEND:-mali}"
export __EGL_VENDOR_LIBRARY_FILENAMES="${__EGL_VENDOR_LIBRARY_FILENAMES:-/opt/cixgpu-compat/share/glvnd/egl_vendor.d/40_cix.json:/usr/share/glvnd/egl_vendor.d/50_mesa.json}"

cd "${RESULTS_ROOT}/bin"
BIN_DIR="${RESULTS_ROOT}/bin" \
SHOT_DIR="${RAW_DIR}/shots" \
LOG_DIR="${RAW_DIR}/logs" \
RESULTS_CSV="${RAW_DIR}/results.csv" \
RUN_SECONDS="${RUN_SECONDS:-2}" \
POST_SECONDS="${POST_SECONDS:-1}" \
SIGKILL_GRACE="${SIGKILL_GRACE:-3}" \
bash "${SRC_DIR}/validation/run-all-gles3.sh"

# Post-run summary
echo ""
echo "=== Run complete for ${platform} ==="
echo "shots: $(ls ${RAW_DIR}/shots/*.png 2>/dev/null | wc -l) PNGs"
echo "logs:  $(ls ${RAW_DIR}/logs/*.stderr 2>/dev/null | wc -l) stderr files"
echo "csv:   ${RAW_DIR}/results.csv ($(wc -l < ${RAW_DIR}/results.csv) lines)"
