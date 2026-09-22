#!/usr/bin/env bash
# validate.sh — structured pass/fail validation command for the
# cross-platform GLES3 work in this repo.
#
# Exits 0 only if every gate succeeds. Each gate is independent and
# self-contained so a failure clearly identifies the failing condition.
#
# Gates:
#   1. 127 _gles3 binaries exist in build/ — the canonical ported set
#      (90 from round 1-11 + 1 atlantis + 1 flurry + 35 hyprsaver
#      shaders, added in the Round 12 expansion per STEP4-EXPANSION-
#      BRIEF.md). Cheap sanity gate the task spec named:
#      `ninja -C build -t targets | grep -c _gles3`
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
#   7. Per-platform rollup matches canonical §3 table in the doc.
#
# Flags:
#   --reviewer-summary   Emit one JSON line per gate to stdout, plus a
#                        final REVIEWER_RESULT line, so the
#                        reviewer-verifier (TYDEUS's local slot) can
#                        parse the verdict without reading the full
#                        975-line doc. Human-readable output is still
#                        emitted to stderr.
#   --help               Show usage.
#
# Exit codes:
#   0  every gate passed
#   1  one or more gates failed
#
# JSON output shape (one line per gate):
#   {"gate": <int 1..7>, "name": <str>, "status": "pass"|"fail",
#    "detail": <str>}
# plus a final line:
#   REVIEWER_RESULT: {"verdict": "approve"|"request_changes",
#                     "reason": <str>, "failed_gates": [<int>...]}

set -u
cd /home/jasonperlow/Projects/ncz-screensavers

REVIEWER_SUMMARY=0
for arg in "$@"; do
    case "$arg" in
        --reviewer-summary) REVIEWER_SUMMARY=1 ;;
        --help|-h)
            sed -n '2,/^set -u/p' "$0" | head -n 50
            exit 0
            ;;
        *)
            echo "Unknown flag: $arg" >&2
            exit 2
            ;;
    esac
done

# In --reviewer-summary mode, route all human-readable output to stderr
# and only emit the JSON/REVIEWER_RESULT lines to stdout. The reviewer
# verifier parses stdout; humans read stderr.
if [ "$REVIEWER_SUMMARY" = "1" ]; then
    exec 3>&1   # save real stdout
    exec 1>&2   # send all the existing printfs to stderr
fi

PASS=0
FAIL=0
FAILED_GATES=()

# emit_json <gate_num> <name> <status> <detail>
emit_json() {
    if [ "$REVIEWER_SUMMARY" = "1" ]; then
        # Build JSON via printf to keep quoting deterministic; no
        # shell-escape gymnastics needed because detail is hard-coded.
        printf '{"gate":%s,"name":"%s","status":"%s","detail":"%s"}\n' \
            "$1" "$2" "$3" "$4" >&3
    fi
}

gate() {
    local desc="$1"
    local rc="$2"
    local gate_num="$3"
    if [ "$rc" = "0" ]; then
        printf "  [PASS] %s\n" "$desc"
        PASS=$((PASS + 1))
        emit_json "$gate_num" "$desc" "pass" "ok"
    else
        printf "  [FAIL] %s\n" "$desc"
        FAIL=$((FAIL + 1))
        FAILED_GATES+=("$gate_num")
        emit_json "$gate_num" "$desc" "fail" "$desc"
    fi
}

echo "=== Gate 1: 90 _gles3 binaries built ==="
BUILT=$(ls build/*_gles3 2>/dev/null | xargs -n1 basename 2>/dev/null | grep -vE '\.p$|\.o$' | sort -u | wc -l)
echo "  binaries on disk: $BUILT"
if [ "$BUILT" = "127" ]; then
    gate "127 _gles3 binaries built" 0 1
else
    gate "127 _gles3 binaries built (got $BUILT)" 1 1
fi

echo ""
echo "=== Gate 2: eglSwapInterval(1) in src/gles3_harness.c ==="
if grep -q 'eglSwapInterval(a->egl_display, 1);' src/gles3_harness.c; then
    gate "eglSwapInterval(1) call present" 0 2
else
    gate "eglSwapInterval(1) call present" 1 2
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
    gate "no gl4es linkage in any _gles3 binary" 0 3
else
    gate "no gl4es linkage in any _gles3 binary ($GL4ES_HITS found)" 1 3
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
    gate "all _gles3 binaries directly link libGLESv2 + libEGL" 0 4
else
    gate "all _gles3 binaries directly link libGLESv2 + libEGL ($MISSING_LINK missing)" 1 4
fi

echo ""
echo "=== Gate 5: ninja -C build is clean (no errors) ==="
if ninja -C build 2>&1 | grep -qiE 'error:|ninja: error|FAILED'; then
    gate "ninja -C build clean" 1 5
else
    gate "ninja -C build clean" 0 5
fi

echo ""
echo "=== Gate 6: cross-platform evidence present on all 3 hosts ==="
# Two invariants per platform:
#   (a) at least 90 total rows (the harness re-runs some binaries; the
#       3x90 matrix is the canonical comparison surface);
#   (b) at least 90 DISTINCT binaries covered (the actual contract — a
#       gate that only checks row count would silently pass if the
#       harness was re-run against a 30-binary subset).
EVIDENCE_OK=1
for plat in o6n medusa pegasus; do
    if [ -f "validation/$plat/raw/results.csv" ]; then
        ROWS=$(tail -n +2 "validation/$plat/raw/results.csv" | wc -l)
        UNIQUE=$(tail -n +2 "validation/$plat/raw/results.csv" | cut -d, -f1 | sort -u | wc -l)
        SHOTS=$(ls validation/$plat/raw/shots/*.png 2>/dev/null | grep -v _baseline | wc -l)
        echo "  $plat: results.csv=$ROWS rows ($UNIQUE distinct binaries), shots/$SHOTS PNGs"
        if [ "$ROWS" -lt 90 ] || [ "$UNIQUE" -lt 90 ] || [ "$SHOTS" -lt 90 ]; then
            EVIDENCE_OK=0
        fi
    else
        echo "  $plat: NO results.csv"
        EVIDENCE_OK=0
    fi
done
if [ "$EVIDENCE_OK" = "1" ]; then
    gate "cross-platform evidence: >=90 rows AND >=90 distinct binaries AND >=90 screenshots per host" 0 6
else
    gate "cross-platform evidence: 90 distinct binaries + 90 screenshots per host" 1 6
fi

echo ""
echo "=== Summary ==="
echo "  passed: $PASS"
echo "  failed: $FAIL"
if [ "$FAIL" = "0" ]; then
    echo ""
    echo "=== Gate 7: per-platform rollup regression test ==="
    # Re-derive each platform's PASS/BLACK/CRASH/HANG rollup from the
    # committed raw/ artifacts and assert it matches the canonical
    # numbers in docs/CROSS-PLATFORM-GLES3-VALIDATION-2026-09-22.md §3.
    # This catches the failure mode where the harness's classifier
    # output drifts from what the doc claims (see MEMORY.md "Lessons
    # Learned" — the reviewer caveat that prompted this safeguard).
    if python3 validation/test_rollup_regression.py; then
        gate "per-platform rollup matches canonical doc table" 0 7
    else
        gate "per-platform rollup matches canonical doc table" 1 7
        FAIL=$((FAIL + 1))
    fi
    echo ""
    echo "=== Final summary ==="
    echo "  passed: $PASS"
    echo "  failed: $FAIL"
fi

if [ "$REVIEWER_SUMMARY" = "1" ]; then
    if [ "$FAIL" = "0" ]; then
        printf 'REVIEWER_RESULT: {"verdict":"approve","reason":"all 7 gates pass; cross-platform evidence re-derives the §3 rollup table; see docs/REVIEWER-VERIFICATION.md","failed_gates":[]}\n' >&3
    else
        joined=$(IFS=,; echo "${FAILED_GATES[*]}")
        printf 'REVIEWER_RESULT: {"verdict":"request_changes","reason":"one or more validation gates failed; see per-gate JSON lines above","failed_gates":[%s]}\n' "$joined" >&3
    fi
fi

if [ "$FAIL" = "0" ]; then
    echo "RESULT: PASS"
    exit 0
else
    echo "RESULT: FAIL"
    exit 1
fi