#!/bin/sh
# Build the Singularity screensaver plugin against a Singularity SDK.
# usage: build-plugin.sh SDK_PREFIX OUT_DIR   (SDK_PREFIX has lib/pkgconfig/singularity-1.0.pc and share/vala/vapi)
# Produces OUT_DIR/libscreensaver.so and OUT_DIR/screensaver.plugin.
set -eu
SDK=${1:?usage: $0 SDK_PREFIX OUT_DIR}
OUT=${2:?usage: $0 SDK_PREFIX OUT_DIR}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
printf "project('ncz-screensaver-plugin', 'c', version: '0.1.0')\nsubdir('plugin')\n" > "$T/meson.build"
cp "$ROOT/meson_options.txt" "$T/meson_options.txt"
ln -s "$ROOT/plugin" "$T/plugin"
PKG_CONFIG_PATH="$SDK/lib/pkgconfig" meson setup "$T/b" "$T" -Dsingularity-plugin=enabled \
    -Dsingularity-vapidir="$SDK/share/vala/vapi" -Db_lundef=false >/dev/null
PKG_CONFIG_PATH="$SDK/lib/pkgconfig" ninja -C "$T/b" >/dev/null
mkdir -p "$OUT"
cp "$T/b/plugin/screensaver/libscreensaver.so" "$ROOT/plugin/screensaver/screensaver.plugin" "$OUT/"
