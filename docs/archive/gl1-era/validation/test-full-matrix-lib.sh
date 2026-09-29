#!/usr/bin/env bash
set -eu

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
. "$SCRIPT_DIR/full-matrix-lib.sh"
work=$(mktemp -d "$HOME/build-tmp/full-matrix-test.XXXXXX")
trap 'rm -rf "$work"' EXIT

cat >"$work/map.stderr" <<'EOF'
[diag] gles3_harness: init returned
ncz-screensavers-glmatrix: running mapscroller.pl: No such file or directory
gles3_harness: eglSwapBuffers failed (0x3000)
EOF
[ "$(matrix_fatal_line "$work/map.stderr")" = \
  "ncz-screensavers-glmatrix: running mapscroller.pl: No such file or directory" ]

cat >"$work/good.stderr" <<'EOF'
[diag] gles3_compat: shader program 3 compiled
[diag] framebuffer frame=4 pixels=2073600 nonblack=100 hash=0123 gl_error=0x0
[diag] framebuffer frame=60 pixels=2073600 nonblack=200 hash=4567 gl_error=0x0
EOF
[ -z "$(matrix_fatal_line "$work/good.stderr")" ]
[ "$(matrix_framebuffer_sample "$work/good.stderr" 60)" = "200 4567 0x0" ]

cat >"$work/shader.stderr" <<'EOF'
gles3_compat: shader compile failed (frag):
bad shader
EOF
[ "$(matrix_fatal_line "$work/shader.stderr")" = \
  "gles3_compat: shader compile failed (frag):" ]

[ "$(matrix_tsv_field $'bad\tline\n')" = "bad line " ]
echo "full-matrix helper tests: PASS"
