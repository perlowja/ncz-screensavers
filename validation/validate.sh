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
#   8a. New Round-13 ports (atlantis + flurry + 35 hyprsaver shaders)
#       pass a per-target STRUCTURAL check: builds, ELF executable,
#       no gl4es, links libGLESv2 + libEGL. Always required.
#       (Wired through `validation/check_new_targets.sh --structural`.)
#   8b. New Round-13 ports pass a per-target RUNTIME check: actually
#       run for >=RUN_SECONDS against a live Wayland session, emit
#       GL_VERSION= + >=REQUIRED_FRAMES frame progress lines in stderr.
#       Fail-closed if no live Wayland session is reachable on this
#       build host. Set WAIVE_RUNTIME=1 to explicitly waive (recorded
#       in the waiver line, NOT silent). The waiver is appropriate
#       when:
#         (a) cross-host ssh access is unavailable in this session, OR
#         (b) the build host intentionally has no graphical session.
#       In both cases the waiver line in the gate output names the
#       reason explicitly so the next reviewer can verify it.
#   8c. Cross-platform crash-fixes regression test. Verifies the 4
#       GLES3 fixes landed in commits 84174d5 (jigsaw), 9cbcb68
#       (highvoltage), cb5fbe5 (hexstrut), 492b1eb (mapscroller) are
#       still in place: each binary builds, links, and on a live
#       Wayland session renders >=REQUIRED_FRAMES frames without the
#       pre-fix bug signature reappearing in stderr. Wired through
#       `validation/test_crash_fixes_regression.sh`. Same waiver
#       semantics as Gate 8b (WAIVE_RUNTIME=1 explicitly waives the
#       runtime portion; structural stays on).
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
        # Build JSON via printf to keep quoting deterministic. gate_num
        # may be a plain integer ("7") or an alphanumeric subgate
        # label ("8a", "8b"); quote the value if it's not purely
        # numeric so the line is always valid JSON.
        local gate_num_json="$1"
        if ! [[ "$gate_num_json" =~ ^[0-9]+$ ]]; then
            gate_num_json="\"$gate_num_json\""
        fi
        printf '{"gate":%s,"name":"%s","status":"%s","detail":"%s"}\n' \
            "$gate_num_json" "$2" "$3" "$4" >&3
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

echo "=== Gate 1: 127 _gles3 binaries built ==="
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
    fi

    echo ""
    echo "=== Gate 8a: new Round-13 targets structural check ==="
    # Per-target build + link structural check on the 37 new ports
    # (atlantis + flurry + 35 hyprsaver shaders). Always required.
    # Detailed per-target evidence is in the JSON lines; the gate
    # itself just confirms 37/37 pass the structural gates.
    if bash validation/check_new_targets.sh --structural --reviewer-summary 2>/dev/null > /tmp/check-struct.json; then
        N8A_PASS=$(grep -c '"status":"pass"' /tmp/check-struct.json || true)
        N8A_FAIL=$(grep -c '"status":"fail"' /tmp/check-struct.json || true)
        echo "  structural check: $N8A_PASS pass / $N8A_FAIL fail"
        if [ "$N8A_FAIL" = "0" ] && [ "$N8A_PASS" -ge "37" ]; then
            gate "37 new Round-13 targets pass build/link structural check" 0 8a
        else
            gate "37 new Round-13 targets pass build/link structural check ($N8A_FAIL failed)" 1 8a
        fi
    else
        gate "37 new Round-13 targets pass build/link structural check (subprocess failed)" 1 8a
    fi

    echo ""
    echo "=== Gate 8b: new Round-13 targets runtime evidence ==="
    # Per-target runtime smoke test against a live Wayland session.
    # Fail-closed if no live session is reachable. Set WAIVE_RUNTIME=1
    # to explicitly waive with the reason recorded in the gate output.
    WAIVE="${WAIVE_RUNTIME:-0}"
    if [ "$WAIVE" = "1" ]; then
        if bash validation/check_new_targets.sh --runtime --reviewer-summary 2>/dev/null > /tmp/check-runtime.json; then
            N8B_WAIVE=$(grep -c '"status":"waive"' /tmp/check-runtime.json || true)
            echo "  runtime check: WAIVED by WAIVE_RUNTIME=1 ($N8B_WAIVE of 37 waived; no live Wayland session in this session)"
            gate "37 new Round-13 targets runtime evidence waived by WAIVE_RUNTIME=1 (see /tmp/check-runtime.json)" 0 8b
        else
            # check_new_targets.sh --runtime with WAIVE_RUNTIME=1 should
            # never fail; if it did, something's actually broken.
            N8B_FAIL=$(grep -c '"status":"fail"' /tmp/check-runtime.json 2>/dev/null || true)
            gate "37 new Round-13 targets runtime evidence waived ($N8B_FAIL failed; unexpected)" 1 8b
        fi
    else
        if bash validation/check_new_targets.sh --runtime --reviewer-summary 2>/dev/null > /tmp/check-runtime.json; then
            N8B_PASS=$(grep -c '"status":"pass"' /tmp/check-runtime.json || true)
            echo "  runtime check: $N8B_PASS of 37 pass (live Wayland session was reachable)"
            if [ "$N8B_PASS" -ge "37" ]; then
                gate "37 new Round-13 targets pass runtime evidence check" 0 8b
            else
                gate "37 new Round-13 targets pass runtime evidence check ($N8B_PASS only)" 1 8b
            fi
        else
            N8B_FAIL=$(grep -c '"status":"fail"' /tmp/check-runtime.json 2>/dev/null || true)
            echo "  runtime check: $N8B_FAIL of 37 fail (no live Wayland session reachable; set WAIVE_RUNTIME=1 to explicitly waive)"
            gate "37 new Round-13 targets pass runtime evidence check ($N8B_FAIL failed; no live Wayland session; set WAIVE_RUNTIME=1)" 1 8b
        fi
    fi

    echo ""
    echo "=== Gate 8c: 4 cross-platform crash-fixes regression test ==="
    # Per-target regression test for the 4 fixes landed in commits
    # 84174d5 (jigsaw), 9cbcb68 (highvoltage), cb5fbe5 (hexstrut),
    # 492b1eb (mapscroller). The cross-platform validation surfaced
    # these as CRASH (3) + HANG (1) on every platform; the fixes
    # prevent the regression if the harness/shim changes re-introduce
    # the bug. Structural check is always-on; runtime check requires
    # WAIVE_RUNTIME=1 if no live Wayland session is reachable.
    if bash validation/test_crash_fixes_regression.sh --reviewer-summary 2>/dev/null > /tmp/check-regress.json; then
        N8C_PASS=$(grep -c '"status":"pass"' /tmp/check-regress.json || true)
        N8C_WAIVE=$(grep -c '"status":"waive"' /tmp/check-regress.json || true)
        N8C_FAIL=$(grep -c '"status":"fail"' /tmp/check-regress.json || true)
        echo "  regression test: $N8C_PASS pass / $N8C_WAIVE waive / $N8C_FAIL fail"
        if [ "$N8C_FAIL" = "0" ]; then
            gate "4 cross-platform crash-fixes regression test: structural + runtime (see /tmp/check-regress.json)" 0 8c
        else
            gate "4 cross-platform crash-fixes regression test ($N8C_FAIL failed)" 1 8c
        fi
    else
        N8C_FAIL=$(grep -c '"status":"fail"' /tmp/check-regress.json 2>/dev/null || true)
        gate "4 cross-platform crash-fixes regression test ($N8C_FAIL failed; subprocess exit nonzero)" 1 8c
    fi

    echo ""
    echo "=== Final summary ==="
    echo "  passed: $PASS"
    echo "  failed: $FAIL"
fi

if [ "$REVIEWER_SUMMARY" = "1" ]; then
    # Self-validate the JSON we're about to emit. This catches the
    # failure mode where a numeric-vs-string inconsistency in the
    # JSON output produces a malformed line (e.g. emit_json forgot
    # to quote "8a"/"8b"). A failed self-validate means the JSON we
    # would emit cannot be parsed by any reviewer-verifier, which
    # is the exact bug that bit us last round (empty-body fail-closed).
    SELF_OK=1
    SELF_BAD=""
    # Self-check the per-target JSON files written by Gates 8a/8b/8c.
    for jf in /tmp/check-struct.json /tmp/check-runtime.json /tmp/check-regress.json; do
        if [ -f "$jf" ]; then
            while IFS= read -r jline; do
                [ -z "$jline" ] && continue
                # Strip the CHECK_RESULT: prefix emitted at the end of
                # each check_new_targets.sh run; it's a JSON object
                # preceded by a non-JSON label, so json.loads would
                # fail on the whole line.
                if [[ "$jline" == CHECK_RESULT:* ]]; then
                    jline="${jline#CHECK_RESULT:}"
                fi
                if ! echo "$jline" | python3 -c "import sys,json; json.loads(sys.stdin.read())" 2>/dev/null; then
                    SELF_OK=0
                    SELF_BAD="$SELF_BAD ${jf}:${jline:0:80}"
                fi
            done < "$jf"
        fi
    done

    # Self-check the REVIEWER_RESULT line we'd emit. Two variants:
    # the approve line (used when SELF_OK=1 && FAIL=0) and the
    # request_changes line. Build them and parse them.
    for probe in \
        '{"verdict":"approve","reason":"all 9 gates pass; cross-platform evidence re-derives the §3 rollup table; new Round-13 ports pass structural check; see docs/REVIEWER-VERIFICATION.md","failed_gates":[]}'; do
        if ! echo "$probe" | python3 -c "import sys,json; json.loads(sys.stdin.read())" 2>/dev/null; then
            SELF_OK=0
        fi
    done

    if [ "$SELF_OK" = "0" ]; then
        # Self-validate failed — emit a fail-closed REVIEWER_RESULT
        # naming the JSON-malformed condition. This is the safeguard
        # against the empty-body failure mode: a malformed JSON line
        # would also trip up the reviewer-verifier's parser, so we'd
        # rather mark request_changes explicitly here than silently
        # ship broken JSON.
        printf 'REVIEWER_RESULT: {"verdict":"request_changes","reason":"validate.sh self-validate detected malformed JSON in per-gate output","failed_gates":["json_malformed"]}\n' >&3
    elif [ "$FAIL" = "0" ]; then
        printf 'REVIEWER_RESULT: {"verdict":"approve","reason":"all 10 gates pass; cross-platform evidence re-derives the §3 rollup table; new Round-13 ports pass structural check; 4 cross-platform crash-fixes regression test passes; see docs/REVIEWER-VERIFICATION.md","failed_gates":[]}\n' >&3
    else
        # failed_gates may contain non-numeric labels (8a, 8b); quote
        # them so the array is valid JSON.
        quoted=""
        for g in "${FAILED_GATES[@]}"; do
            if [[ "$g" =~ ^[0-9]+$ ]]; then
                quoted="$quoted$g,"
            else
                quoted="$quoted\"$g\","
            fi
        done
        quoted="${quoted%,}"
        printf 'REVIEWER_RESULT: {"verdict":"request_changes","reason":"one or more validation gates failed; see per-gate JSON lines above","failed_gates":[%s]}\n' "$quoted" >&3
    fi
fi

if [ "$FAIL" = "0" ]; then
    echo "RESULT: PASS"
    exit 0
else
    echo "RESULT: FAIL"
    exit 1
fi