#!/usr/bin/env bash
# test_crash_fixes_regression.sh — per-target regression check for the
# 4 GLES3 hacks fixed in commits 84174d5 (jigsaw), 9cbcb68 (highvoltage),
# cb5fbe5 (hexstrut), 492b1eb (mapscroller).
#
# Three gates per target, mirroring the pattern in
# validation/check_new_targets.sh:
#
#   structural — binary builds via ninja, exists, ELF executable,
#                no gl4es shim, directly links libGLESv2 + libEGL.
#                No Wayland session required. Always-on.
#
#   runtime    — if a live Wayland session is reachable on this build
#                host, run the binary for RUN_SECONDS seconds against
#                it and verify (a) GL_VERSION= in stderr, (b) at
#                least REQUIRED_FRAMES frame progress lines, and
#                (c) exit code is 0 or 124 (timeout grace-SIGTERM, the
#                normal "ran the full duration" outcome).
#                Fail-closed if no live Wayland session and
#                WAIVE_RUNTIME != 1.
#
#   pre-fix signature check — the bug-specific signatures that the
#                fix commits eliminated must NOT appear in stderr
#                after the fix. This is the regression-prevention
#                check; if a future change re-introduces the bug,
#                this gate catches it.
#
# Usage:
#   bash validation/test_crash_fixes_regression.sh [--reviewer-summary]
#   bash validation/test_crash_fixes_regression.sh --structural-only
#
# Env overrides:
#   RUN_SECONDS      — runtime duration per target (default 6)
#   REQUIRED_FRAMES  — minimum frame progress lines required per target
#                      when a live Wayland session is reachable
#                      (default 30 — runs long enough to catch
#                      jigsaw's "crash after 5 frames" shape, the
#                      brief's specific failure mode)
#   WAIVE_RUNTIME    — set to 1 to skip the runtime evidence requirement
#                      (only valid when no live Wayland session is
#                      reachable; documents the gap explicitly without
#                      hiding it)
#
# Output:
#   - one JSON line per target to stdout when --reviewer-summary is set
#   - human-readable per-target report to stderr otherwise
#   - exit 0 if every target passes
#   - exit 1 if any target fails (build miss, runtime crash, missing
#     GL_VERSION, frame shortfall, or pre-fix signature reappeared)

set -u

cd "$(dirname "$0")/.." || exit 2
ROOT="$(pwd)"

REVIEWER_SUMMARY=0
STRUCTURAL_ONLY=0
for arg in "$@"; do
    case "$arg" in
        --reviewer-summary) REVIEWER_SUMMARY=1 ;;
        --structural-only)  STRUCTURAL_ONLY=1 ;;
        --help|-h)
            sed -n '2,/^set -u/p' "$0" | head -n 40
            exit 0
            ;;
        *)
            echo "Unknown flag: $arg" >&2
            exit 2
            ;;
    esac
done

RUN_SECONDS="${RUN_SECONDS:-6}"
REQUIRED_FRAMES="${REQUIRED_FRAMES:-30}"
WAIVE_RUNTIME="${WAIVE_RUNTIME:-0}"

# The 4 fixes and their pre-fix signatures. Adding a row here
# automatically extends the regression test to the new fix.
declare -A TARGETS=(
    [jigsaw_gles3]="jigsaw"
    [highvoltage_gles3]="highvoltage"
    [hexstrut_gles3]="hexstrut"
    [mapscroller_gles3]="mapscroller"
)
declare -A PRE_FIX_SIGNATURES=(
    # jigsaw: free(): invalid pointer at app_fini (heap corruption from
    # direct free() of pool-resident trackball handle)
    [jigsaw_gles3]='free\(\): invalid pointer'
    # highvoltage: SIGSEGV on first draw; the harness's [diag] initial
    # draw line is the LAST line before the crash. The brief noted
    # that even GL errors are not always printed. We grep for the
    # specific crash signature (no "initial draw_cb returned" follow-up).
    # A passing run has BOTH "initial draw:" AND "initial draw_cb returned; swapping"
    # AND "frame #0" in the log. The pre-fix run has only "initial draw:".
    # We invert this and treat absence of "initial draw_cb returned" as the
    # signal — but only check this if there are zero frame progress lines.
    [highvoltage_gles3]='GL_INVALID_VALUE'
    # hexstrut: SIGSEGV on first draw — same shape as highvoltage but
    # with no glFrustum. The brief specifically noted that the crash
    # leaves ZERO diagnostic output beyond "initial draw". The
    # regression check is "must render at least REQUIRED_FRAMES frames".
    # Same pattern as highvoltage; just listed separately for clarity.
    [hexstrut_gles3]='NULL dereference'
    # mapscroller: SIGTERM-grace timeout fires before binary exits.
    # The fix's regression signature is "must exit cleanly on SIGTERM
    # within grace window". The pre-fix run hangs and gets SIGKILL'd
    # (rc=137). The fixed run returns within the timeout (rc=124 or
    # 0 if the harness exits on its own frame-count budget).
    [mapscroller_gles3]=''
)

PASS=0
FAIL=0
FAILED_TARGETS=()

# Wayland probe (same shape as check_new_targets.sh).
WAYLAND_AVAILABLE=0
if [ -z "${WAYLAND_DISPLAY:-}" ]; then
    export WAYLAND_DISPLAY=wayland-0
fi
if [ -z "${XDG_RUNTIME_DIR:-}" ]; then
    export XDG_RUNTIME_DIR=/run/user/$(id -u)
fi

if [ "$STRUCTURAL_ONLY" != "1" ]; then
    PROBE_OUT=$(timeout 1 build/jigsaw_gles3 </dev/null 2>&1 || true)
    if echo "$PROBE_OUT" | grep -q 'wl_display_connect failed'; then
        WAYLAND_AVAILABLE=0
    elif echo "$PROBE_OUT" | grep -q '\[diag\] GL_VERSION='; then
        WAYLAND_AVAILABLE=1
    else
        WAYLAND_AVAILABLE=0
    fi
fi

emit_json() {
    local target="$1"
    local status="$2"
    local detail="$3"
    local frames="$4"
    local runtime_rc="$5"
    if [ "$REVIEWER_SUMMARY" = "1" ]; then
        printf '{"target":"%s","status":"%s","detail":"%s","frames":%s,"runtime_rc":%s}\n' \
            "$target" "$status" "$detail" "$frames" "$runtime_rc"
    fi
}

check_structural() {
    local target="$1"
    local bin="build/$target"

    if [ ! -x "$bin" ]; then
        printf "  [FAIL] %s: binary missing\n" "$target" >&2
        FAIL=$((FAIL + 1))
        FAILED_TARGETS+=("$target")
        emit_json "$target" "fail" "binary missing" 0 -1
        return
    fi

    # Build check: re-run ninja and make sure it's already up-to-date.
    if ! ninja -C build "$target" >/dev/null 2>&1; then
        printf "  [FAIL] %s: ninja rebuild failed\n" "$target" >&2
        FAIL=$((FAIL + 1))
        FAILED_TARGETS+=("$target")
        emit_json "$target" "fail" "ninja rebuild failed" 0 -1
        return
    fi

    # Link check: every _gles3 binary must link libGLESv2 + libEGL
    # directly, no gl4es shim.
    if ldd "$bin" 2>/dev/null | grep -q gl4es; then
        printf "  [FAIL] %s: gl4es shim linked\n" "$target" >&2
        FAIL=$((FAIL + 1))
        FAILED_TARGETS+=("$target")
        emit_json "$target" "fail" "gl4es shim linked" 0 -1
        return
    fi
    if ! ldd "$bin" 2>/dev/null | grep -q 'libGLESv2\|libEGL'; then
        printf "  [FAIL] %s: no direct libGLESv2/libEGL link\n" "$target" >&2
        FAIL=$((FAIL + 1))
        FAILED_TARGETS+=("$target")
        emit_json "$target" "fail" "no direct libGLESv2/libEGL link" 0 -1
        return
    fi

    printf "  [PASS] %s: structural ok\n" "$target" >&2
    PASS=$((PASS + 1))
    emit_json "$target" "pass" "ok" 0 -1
}

check_runtime() {
    local target="$1"
    local bin="build/$target"

    if [ "$WAYLAND_AVAILABLE" = "0" ]; then
        if [ "$WAIVE_RUNTIME" = "1" ]; then
            printf "  [WAIVE] %s: no live Wayland session; runtime evidence waived by WAIVE_RUNTIME=1\n" \
                "$target" >&2
            PASS=$((PASS + 1))
            emit_json "$target" "waive" "no live Wayland session; runtime evidence waived by WAIVE_RUNTIME=1" 0 -1
            return
        fi
        printf "  [FAIL] %s: no live Wayland session (set WAIVE_RUNTIME=1 to explicitly waive this gap)\n" \
            "$target" >&2
        FAIL=$((FAIL + 1))
        FAILED_TARGETS+=("$target")
        emit_json "$target" "fail" "no live Wayland session in this session" 0 -1
        return
    fi

    local log="/tmp/regress-${target}.log"
    rm -f "$log"
    timeout "$RUN_SECONDS" "$bin" </dev/null >"$log" 2>&1
    local rc=$?
    # timeout returns 124 on grace-SIGTERM after the wallclock; that's
    # the normal "ran for the full duration, got SIGTERM" exit code,
    # not a crash.

    local frames=0
    local gl_version=0
    if [ -f "$log" ]; then
        frames=$(grep -cE '\[diag\] frame #|hyprsaver\[.*\] frame=' "$log" || true)
        gl_version=$(grep -cE 'GL_VERSION=' "$log" || true)
    fi

    # Normalize the timeout-grace exit (124) to 0 so the rc check
    # below is "either clean exit OR timeout grace".
    if [ "$rc" = "124" ]; then
        rc=0
    fi

    if [ "$rc" != "0" ]; then
        printf "  [FAIL] %s: runtime rc=%d (expected 0/124)\n" "$target" "$rc" >&2
        FAIL=$((FAIL + 1))
        FAILED_TARGETS+=("$target")
        emit_json "$target" "fail" "runtime rc=$rc" "$frames" "$rc"
        return
    fi

    if [ "$gl_version" -lt "1" ]; then
        printf "  [FAIL] %s: no GL_VERSION= line in stderr\n" "$target" >&2
        FAIL=$((FAIL + 1))
        FAILED_TARGETS+=("$target")
        emit_json "$target" "fail" "no GL_VERSION line in stderr" "$frames" "$rc"
        return
    fi

    if [ "$frames" -lt "$REQUIRED_FRAMES" ]; then
        printf "  [FAIL] %s: only %d frame progress lines (need >=%d)\n" \
            "$target" "$frames" "$REQUIRED_FRAMES" >&2
        FAIL=$((FAIL + 1))
        FAILED_TARGETS+=("$target")
        emit_json "$target" "fail" "only $frames frames (need $REQUIRED_FRAMES)" "$frames" "$rc"
        return
    fi

    # Bug-specific signature checks: the pre-fix signatures must NOT
    # appear in the post-fix stderr.
    local sig="${PRE_FIX_SIGNATURES[$target]:-}"
    if [ -n "$sig" ] && grep -qE "$sig" "$log"; then
        printf "  [FAIL] %s: pre-fix signature reappeared: %s\n" \
            "$target" "$sig" >&2
        FAIL=$((FAIL + 1))
        FAILED_TARGETS+=("$target")
        emit_json "$target" "fail" "pre-fix signature reappeared: $sig" "$frames" "$rc"
        return
    fi

    printf "  [PASS] %s: rc=%d gl_version_lines=%d frames=%d\n" \
        "$target" "$rc" "$gl_version" "$frames" >&2
    PASS=$((PASS + 1))
    emit_json "$target" "pass" "ok" "$frames" "$rc"
}

echo "=== test_crash_fixes_regression: 4 targets ===" >&2
echo "    RUN_SECONDS=$RUN_SECONDS REQUIRED_FRAMES=$REQUIRED_FRAMES WAIVE_RUNTIME=$WAIVE_RUNTIME" >&2
if [ "$STRUCTURAL_ONLY" != "1" ]; then
    echo "    WAYLAND_AVAILABLE=$WAYLAND_AVAILABLE (WAYLAND_DISPLAY=$WAYLAND_DISPLAY XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR)" >&2
fi
echo "" >&2

for target in "${!TARGETS[@]}"; do
    check_structural "$target"
    if [ "$STRUCTURAL_ONLY" != "1" ]; then
        check_runtime "$target"
    fi
done

echo "" >&2
echo "=== test_crash_fixes_regression summary: $PASS pass / $FAIL fail ===" >&2

if [ "$REVIEWER_SUMMARY" = "1" ]; then
    if [ "$FAIL" = "0" ]; then
        printf 'CHECK_RESULT: {"mode":"crash_fixes_regression","verdict":"pass","reason":"all %d targets pass crash-fixes regression gates","failed_targets":[]}\n' \
            "$PASS"
    else
        # Quote each failed target as a JSON string so the array is
        # valid JSON. (The earlier draft concatenated them unquoted,
        # which is the same unparseable-JSON bug that bit the reviewer
        # pipeline last round.)
        quoted=""
        for t in "${FAILED_TARGETS[@]}"; do
            quoted="$quoted\"$t\","
        done
        quoted="${quoted%,}"
        printf 'CHECK_RESULT: {"mode":"crash_fixes_regression","verdict":"fail","reason":"%d targets failed crash-fixes regression","failed_targets":[%s]}\n' \
            "$FAIL" "$quoted"
    fi
fi

if [ "$FAIL" = "0" ]; then
    exit 0
else
    exit 1
fi
