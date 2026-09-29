#!/bin/bash
# build-on-host.sh — run on the remote amd64 host (MEDUSA or PEGASUS) to
# build the 90 _gles3 binaries natively.
#
# Usage (called from local):
#   sshpass -p <pw> ssh user@host 'bash -s' < validation/build-on-host.sh
#
# Outputs:
#   ~/gles3-validation/bin/<hack>_gles3  — 90 executables
#
set -e
SRC_DIR="${SRC_DIR:-/tmp/ncz-screensavers}"
BUILD_DIR="${BUILD_DIR:-/tmp/ncz-screensavers/build}"

# Install build deps if missing (apt-based)
if command -v apt-get >/dev/null 2>&1; then
    if ! pkg-config --exists wayland-client egl glesv2 libpng 2>/dev/null; then
        echo "Installing build dependencies..."
        apt-get update -qq
        DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
            build-essential meson ninja-build pkg-config \
            libwayland-dev libegl1-mesa-dev libgles2-mesa-dev libpng-dev \
            libglu1-mesa-dev
    fi
fi

# Fetch source
if [ ! -d "$SRC_DIR" ]; then
    git clone https://example.invalid/ncz-screensavers "$SRC_DIR" 2>/dev/null || \
    cp -r /path/to/ncz-screensavers "$SRC_DIR"
fi

cd "$SRC_DIR"
git fetch --all 2>/dev/null || true
git checkout master 2>/dev/null || true
git pull 2>/dev/null || true

# Clean build
rm -rf "$BUILD_DIR"
meson setup "$BUILD_DIR" -Dgl4es=disabled -Dxscreensaver-shim=disabled 2>&1 | tail -10

# Build all _gles3
GLES3_TARGETS=$(ninja -C "$BUILD_DIR" -t targets all 2>&1 | \
    grep -E "^[a-z][a-z0-9_-]*_gles3: c_LINKER$" | sed 's/: c_LINKER$//')
ninja -C "$BUILD_DIR" $GLES3_TARGETS 2>&1 | tail -5

# Stage to bin/
mkdir -p ~/gles3-validation/bin
for bin in $GLES3_TARGETS; do
    cp -f "$BUILD_DIR/$bin" ~/gles3-validation/bin/
done

echo ""
echo "Built and staged $BUILD_DIR → ~/gles3-validation/bin"
ls ~/gles3-validation/bin | wc -l
