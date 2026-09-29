#!/usr/bin/env bash

# Return the first stderr line that is known to make a GLES3 run invalid.
# Keep this list tied to diagnostics emitted by gles3_harness.c,
# gles3_compat.c, and fatal asset-loading paths in the hacks.
matrix_fatal_line() {
    local log=$1
    grep -Eim1 \
        '^(gles3_harness:|.*(No such file or directory|cannot locate|cannot open|failed to load)|gles3_compat: (shader compile|program link) failed)' \
        "$log" 2>/dev/null || true
}

# Print: frame nonblack hash gl_error
matrix_framebuffer_sample() {
    local log=$1 frame=$2
    sed -nE \
        "s/^\[diag\] framebuffer frame=${frame} pixels=[0-9]+ nonblack=([0-9]+) hash=([^ ]+) gl_error=([^ ]+).*$/\1 \2 \3/p" \
        "$log" | tail -n1
}

matrix_tsv_field() {
    printf '%s' "$1" | tr '\t\r\n' '   '
}
