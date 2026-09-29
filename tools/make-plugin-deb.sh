#!/bin/sh
# make-plugin-deb.sh - package the Singularity screensaver plugin.
# usage: make-plugin-deb.sh ARCH BUILD_DIR OUT.deb [VERSION]
#   BUILD_DIR holds libscreensaver.so and screensaver.plugin (tools/build-plugin.sh output).
# The package installs into the plugin directory of the Singularity desktop next to the
# sibling plugins; the shell loads it at its next start (log out and in).
set -eu
ARCH=${1:?arch (amd64|arm64)}
SRC=${2:?build dir}
OUT=${3:?output .deb}
VER=${4:-0.2.0}
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
D=$W/opt/singularity/lib/singularity/plugins/screensaver
mkdir -p "$D" "$W/DEBIAN"
install -m 0755 "$SRC/libscreensaver.so" "$D/libscreensaver.so"
install -m 0644 "$SRC/screensaver.plugin" "$D/screensaver.plugin"
cat > "$W/DEBIAN/control" <<EOF
Package: ncz-screensavers-plugin
Version: $VER
Architecture: $ARCH
Maintainer: Jason Perlow <jperlow@gmail.com>
Depends: ncz-screensavers (>= 0.5.3), ncz-singularity-desktop
Section: x11
Priority: optional
Description: Screensaver and Lockscreen page for the Singularity desktop
 A Singularity plugin (libsingularity widgets only) that adds the "Screensaver and
 Lockscreen" page to Settings > Plugins: chooser, previews, idle timers, GPU class
 flags, offload, per-hack options and the screen lock timers.
EOF
dpkg-deb --root-owner-group -b "$W" "$OUT" >/dev/null
echo "$OUT"
