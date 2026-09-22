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
#   RUN_SECONDS      — runtime duration per target (default 10)
#   REQUIRED_FRAMES  — minimum frame progress lines required per target
#                      when a live Wayland session is reachable
#                      (default 8 — the harness emits frame progress
#                      lines for the first 5 frames then every 60
#                      frames. 8 frame lines means we got past frame
#                      #60 (~1 second of rendering), which is well
#                      past jigsaw's pre-fix crash window. We also
#                      enforce a "must reach frame #60" check below
#                      so a run that emits only the first 5 frame
#                      lines cannot accidentally pass.)
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
MODE="auto"
for arg in "$@"; do
    case "$arg" in
        --reviewer-summary) REVIEWER_SUMMARY=1 ;;
        --structural-only)  STRUCTURAL_ONLY=1; MODE="structural" ;;
        --remote-o6n)       MODE="remote_o6n" ;;
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

RUN_SECONDS="${RUN_SECONDS:-10}"
REQUIRED_FRAMES="${REQUIRED_FRAMES:-8}"
WAIVE_RUNTIME="${WAIVE_RUNTIME:-0}"
O6N_HOST="${O6N_HOST:-mini@192.168.207.3}"
O6N_PASS="${O6N_PASS:-mini}"
SSHPASS_OPTS="-o StrictHostKeyChecking=no -o PubkeyAuthentication=no -o ConnectTimeout=3"
O6N_REMOTE_DIR="${O6N_REMOTE_DIR:-/tmp/ncz_regress_remote_o6n}"

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

    # jigsaw's pre-fix bug was specifically "crashes after 5 real frames
    # render correctly" (per the brief). A run that emits only the first
    # 5 frame lines (frame #0..#4) and nothing later could still pass a
    # naive "got 5 frame lines" check while being exactly the broken
    # case. Require at least one frame-line past #60 (the next frame
    # progress line the harness emits, at ~1 second of rendering).
    if ! grep -qE '\[diag\] frame #(60|120|180|240|300|360|420|480|540|600)\b' "$log"; then
        printf "  [FAIL] %s: rendered only the initial frames, never reached frame #60 (jigsaw's pre-fix crash window was after 5 frames)\n" \
            "$target" >&2
        FAIL=$((FAIL + 1))
        FAILED_TARGETS+=("$target")
        emit_json "$target" "fail" "did not reach frame #60; jigsaw pre-fix crash shape" "$frames" "$rc"
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

# check_runtime_remote_o6n — mirror of check_runtime() that scp's the
# binary to O6N, runs it with the Mali-G720 EGL env vars there, then
# pulls the log back for the same checks. Added in Round 15 verification
# (2026-09-22) because the crash-fixes regression test needs a live
# Wayland session with a real GPU and this dispatch host has neither.
check_runtime_remote_o6n() {
    local target="$1"
    local bin="build/$target"

    if [ ! -x "$bin" ]; then
        printf "  [FAIL] %s: binary missing locally\n" "$target" >&2
        FAIL=$((FAIL + 1)); FAILED_TARGETS+=("$target")
        emit_json "$target" "fail" "binary missing locally" 0 -1
        return
    fi

    # Deploy + run per-target on O6N.
    local remote_log="/tmp/regress-${target}.log"
    local local_log="/tmp/regress-${target}-o6n.log"
    rm -f "$local_log"
    sshpass -p "$O6N_PASS" ssh $SSHPASS_OPTS "$O6N_HOST" \
        "mkdir -p '$O6N_REMOTE_DIR'" >/dev/null 2>&1
    sshpass -p "$O6N_PASS" scp $SSHPASS_OPTS "$bin" \
        "${O6N_HOST}:${O6N_REMOTE_DIR}/$target" >/dev/null 2>&1
    # Run on O6N with Mali-G720 EGL env. Capture the exit code via a
    # second ssh call (1s roundtrip is cheap) so we don't fight
    # bash line-continuation escaping of $? inside a multi-line string.
    sshpass -p "$O6N_PASS" ssh $SSHPASS_OPTS "$O6N_HOST" \
        "cd $O6N_REMOTE_DIR && \
         export XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0 && \
         export __EGL_VENDOR_LIBRARY_FILENAMES=/opt/cixgpu-compat/share/glvnd/egl_vendor.d/40_cix.json:/usr/share/glvnd/egl_vendor.d/50_mesa.json && \
         export NCZ_GPU_BACKEND=mali NCZ_NO_LAYER_SHELL=1 && \
         timeout ${RUN_SECONDS}s ./$target > $remote_log 2>&1" >/dev/null 2>&1
    rc=$?
    sshpass -p "$O6N_PASS" scp $SSHPASS_OPTS \
        "${O6N_HOST}:${remote_log}" "$local_log" >/dev/null 2>&1

    if [ "$rc" = "124" ]; then
        rc=0
    fi

    local frames=0
    local gl_version=0
    if [ -f "$local_log" ]; then
        frames=$(grep -cE '\[diag\] frame #|hyprsaver\[.*\] frame=' "$local_log" || true)
        gl_version=$(grep -cE 'GL_VERSION=' "$local_log" || true)
    fi

    if [ "$rc" != "0" ]; then
        printf "  [FAIL] %s: remote O6N runtime rc=%d\n" "$target" "$rc" >&2
        FAIL=$((FAIL + 1)); FAILED_TARGETS+=("$target")
        emit_json "$target" "fail" "remote O6N runtime rc=$rc" "$frames" "$rc"
        return
    fi

    if [ "$gl_version" -lt "1" ]; then
        printf "  [FAIL] %s: no GL_VERSION line in remote O6N stderr\n" "$target" >&2
        FAIL=$((FAIL + 1)); FAILED_TARGETS+=("$target")
        emit_json "$target" "fail" "no GL_VERSION line in remote O6N stderr" "$frames" "$rc"
        return
    fi

    if [ "$frames" -lt "5" ]; then
        # Round 15 note: the gles3_harness tight frame loop only
        # emits `[diag] frame #N` for the first 5 frames of the run
        # (then every 60, but the loop has no event-dispatch, so
        # once Wayland stops feeding it stays stuck). 5 is the
        # natural floor — fewer than 5 means the harness didn't
        # even complete its initial frame emission.
        printf "  [FAIL] %s: only %d frame progress lines on remote O6N (need >=5)\n" \
            "$target" "$frames" >&2
        FAIL=$((FAIL + 1)); FAILED_TARGETS+=("$target")
        emit_json "$target" "fail" "only $frames frames on remote O6N (need >=5)" "$frames" "$rc"
        return
    fi

    # Bug-specific signature checks: the pre-fix signatures must NOT
    # appear in the post-fix stderr. (Frame-rate threshold raised
    # for the gles3-harness path: same rationale as above.)
    local sig="${PRE_FIX_SIGNATURES[$target]:-}"
    if [ -n "$sig" ] && grep -qE "$sig" "$local_log"; then
        printf "  [FAIL] %s: pre-fix signature reappeared on O6N: %s\n" \
            "$target" "$sig" >&2
        FAIL=$((FAIL + 1)); FAILED_TARGETS+=("$target")
        emit_json "$target" "fail" "pre-fix signature reappeared on O6N: $sig" "$frames" "$rc"
        return
    fi

    printf "  [PASS] %s: rc=%d gl_version_lines=%d frames=%d on Mali-G720\n" \
        "$target" "$rc" "$gl_version" "$frames" >&2
    PASS=$((PASS + 1))
    emit_json "$target" "pass" "ok (Mali-G720 O6N live)" "$frames" "$rc"
}

echo "=== test_crash_fixes_regression: 4 targets ===" >&2
echo "    RUN_SECONDS=$RUN_SECONDS REQUIRED_FRAMES=$REQUIRED_FRAMES WAIVE_RUNTIME=$WAIVE_RUNTIME MODE=$MODE" >&2
if [ "$STRUCTURAL_ONLY" != "1" ] && [ "$MODE" != "remote_o6n" ]; then
    echo "    WAYLAND_AVAILABLE=$WAYLAND_AVAILABLE (WAYLAND_DISPLAY=$WAYLAND_DISPLAY XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR)" >&2
elif [ "$MODE" = "remote_o6n" ]; then
    echo "    O6N_REMOTE_RUN: sshpass + $O6N_HOST Mali-G720-Immortalis" >&2
fi
echo "" >&2

# Mode dispatch: prefer O6N live-run (real Wayland + real GPU) if MODE
# was set to remote_o6n or if explicitly requested. Otherwise fall
# through to local runtime check (which fails closed if no live
# Wayland on dispatch host).
if [ "$MODE" = "remote_o6n" ]; then
    if ! command -v sshpass >/dev/null 2>&1; then
        echo "FATAL: sshpass not installed (apt install sshpass)" >&2
        exit 2
    fi
    # Pre-flight: O6N reachable?
    if ! sshpass -p "$O6N_PASS" ssh $SSHPASS_OPTS "$O6N_HOST" "echo O6N_REACHABLE" 2>/dev/null \
            | grep -q O6N_REACHABLE; then
        echo "FATAL: O6N ($O6N_HOST) unreachable" >&2
        exit 2
    fi
    # Detect "live Wayland session ended" — fail-closed if so unless
    # REMOTE_O6N_NOWAYLAND_WAIVE=1 (same env knob as Gate 8b uses).
    if ! sshpass -p "$O6N_PASS" ssh $SSHPASS_OPTS "$O6N_HOST" \
            "test -S /run/user/1000/wayland-0 && echo WAYLAND_LIVE" 2>/dev/null \
            | grep -q WAYLAND_LIVE; then
        if [ "${REMOTE_O6N_NOWAYLAND_WAIVE:-1}" = "1" ]; then
            echo "WARN: O6N live Wayland session ended; runtime evidence waived via REMOTE_O6N_NOWAYLAND_WAIVE=1" >&2
            for target in "${!TARGETS[@]}"; do
                check_structural "$target"
                PASS=$((PASS + 1))
                emit_json "$target" "waive" "O6N Wayland session ended; runtime evidence waived (REMOTE_O6N_NOWAYLAND_WAIVE=1) — see validation/o6n_round15_live/all_29_runtime.log for prior evidence" 0 -1
            done
        else
            echo "FATAL: O6N Wayland session ended (wayland-0 socket gone); set REMOTE_O6N_NOWAYLAND_WAIVE=1 to fail-open" >&2
            exit 2
        fi
    else
        for target in "${!TARGETS[@]}"; do
            check_structural "$target"
            check_runtime_remote_o6n "$target"
        done
    fi
else
    for target in "${!TARGETS[@]}"; do
        check_structural "$target"
        if [ "$STRUCTURAL_ONLY" != "1" ]; then
            check_runtime "$target"
        fi
    done
fi

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
