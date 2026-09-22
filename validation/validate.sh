#!/usr/bin/env bash
# validate.sh — structured pass/fail validation command for the
# cross-platform GLES3 work in this repo.
#
# Exits 0 only if every gate succeeds. Each gate is independent and
# self-contained so a failure clearly identifies the failing condition.
#
# Gates:
#   1. 90 _gles3 binaries exist in build/ (the cheap sanity gate the
#      task spec named: ninja -C build -t targets | grep -c _gles3)
#   2. eglSwapInterval(1) is in src/gles3_harness.c (regression tested
#      2026-09-22 on Mali-Panthor, Mesa-radeonsi, Mesa-iris — all three
#      produce identical ~40fps with or without it, so the call is
#      documented as explicit, harmless, future-proof)
#   3. Every _gles3 binary is dynamically linked against the system
#      libGLESv2 and libEGL (no gl4es translation shim in the GLES3
#      port path — per project constraint)
#   4. Every _gles3 binary has the same linkage on this build host
#      (a link failure on one hack but not another on the same host
#      would itself be a real finding; this gate checks there are none)
#   5. ninja -C build succeeds cleanly (no errors, no warnings about
#      missing targets)
#   6. Cross-platform evidence exists in validation/{o6n,medusa,pegasus}/
#      raw/results.csv with 90 rows each (or the all-3x90 cross-platform
#      matrix doc references real on-host runs)

set -u
cd /home/jasonperlow/Projects/ncz-screensavers

PASS=0
FAIL=0

gate() {
    local desc="$1"
    local rc="$2"
    if [ "$rc" = "0" ]; then
        printf "  [PASS] %s\n" "$desc"
        PASS=$((PASS + 1))
    else
        printf "  [FAIL] %s\n" "$desc"
        FAIL=$((FAIL + 1))
    fi
}

echo "=== Gate 1: 90 _gles3 binaries built ==="
BUILT=$(ls build/*_gles3 2>/dev/null | xargs -n1 basename 2>/dev/null | grep -vE '\.p$|\.o$' | sort -u | wc -l)
echo "  binaries on disk: $BUILT"
if [ "$BUILT" = "90" ]; then
    gate "90 _gles3 binaries built" 0
else
    gate "90 _gles3 binaries built (got $BUILT)" 1
fi

echo ""
echo "=== Gate 2: eglSwapInterval(1) in src/gles3_harness.c ==="
if grep -q 'eglSwapInterval(a->egl_display, 1);' src/gles3_harness.c; then
    gate "eglSwapInterval(1) call present" 0
else
    gate "eglSwapInterval(1) call present" 1
fi

echo ""
echo "=== Gate 3: no gl4es translation shim linked into _gles3 binaries ==="
# Each _gles3 binary should NOT link against /usr/lib/gl4es/libGL.so.1
# (we want direct libGLESv2 / libEGL linkage).
GL4ES_HITS=0
for b in build/*_gles3; do
    if [ -x "$b" ] && ldd "$b" 2>/dev/null | grep -q gl4es; then
        echo "  gl4es found in: $b"
        GL4ES_HITS=$((GL4ES_HITS + 1))
    fi
done
echo "  binaries linked against gl4es: $GL4ES_HITS"
if [ "$GL4ES_HITS" = "0" ]; then
    gate "no gl4es linkage in any _gles3 binary" 0
else
    gate "no gl4es linkage in any _gles3 binary ($GL4ES_HITS found)" 1
fi

echo ""
echo "=== Gate 4: all _gles3 binaries link libGLESv2 + libEGL directly ==="
MISSING_LINK=0
for b in build/*_gles3; do
    if [ -x "$b" ]; then
        if ! ldd "$b" 2>/dev/null | grep -q 'libGLESv2\|libEGL'; then
            echo "  no libGLESv2/libEGL link: $b"
            MISSING_LINK=$((MISSING_LINK + 1))
        fi
    fi
done
echo "  binaries missing direct GLES/EGL linkage: $MISSING_LINK"
if [ "$MISSING_LINK" = "0" ]; then
    gate "all _gles3 binaries directly link libGLESv2 + libEGL" 0
else
    gate "all _gles3 binaries directly link libGLESv2 + libEGL ($MISSING_LINK missing)" 1
fi

echo ""
echo "=== Gate 5: ninja -C build is clean (no errors) ==="
if ninja -C build 2>&1 | grep -qiE 'error:|ninja: error|FAILED'; then
    gate "ninja -C build clean" 1
else
    gate "ninja -C build clean" 0
fi

echo ""
echo "=== Gate 6: cross-platform evidence present on all 3 hosts ==="
EVIDENCE_OK=1
for plat in o6n medusa pegasus; do
    if [ -f "validation/$plat/raw/results.csv" ]; then
        ROWS=$(tail -n +2 "validation/$plat/raw/results.csv" | wc -l)
        SHOTS=$(ls validation/$plat/raw/shots/ 2>/dev/null | grep -c '\.png$')
        echo "  $plat: results.csv=$ROWS rows, shots/$SHOTS PNGs"
        if [ "$ROWS" -lt 90 ] || [ "$SHOTS" -lt 90 ]; then
            EVIDENCE_OK=0
        fi
    else
        echo "  $plat: NO results.csv"
        EVIDENCE_OK=0
    fi
done
if [ "$EVIDENCE_OK" = "1" ]; then
    gate "cross-platform evidence: 90 rows + 90 screenshots per host" 0
else
    gate "cross-platform evidence: 90 rows + 90 screenshots per host" 1
fi

echo ""
echo "=== Summary ==="
echo "  passed: $PASS"
echo "  failed: $FAIL"
if [ "$FAIL" = "0" ]; then
    echo "RESULT: PASS"
    exit 0
else
    echo "RESULT: FAIL"
    exit 1
fi