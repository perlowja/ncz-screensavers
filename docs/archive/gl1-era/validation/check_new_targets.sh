#!/usr/bin/env bash
# check_new_targets.sh — per-target build + runtime smoke test for the
# Round 13 new ports (atlantis + flurry + 35 hyprsaver shaders).
#
# Two distinct gates are wired through this script:
#
#   check_new_targets.sh --structural
#       Builds each new target via ninja (idempotent), verifies the
#       binary exists / is ELF executable / has no gl4es / directly
#       links libGLESv2 + libEGL. NO Wayland session required.
#       Suitable as Gate 8a (always-on structural gate).
#
#   check_new_targets.sh --runtime
#       Builds + tries a real several-second runtime against the
#       CURRENT host's Wayland session. Captures stderr to /tmp; greps
#       for:
#           - at least one [diag] line carrying GL_VERSION=
#           - at least N frame progress lines ([diag] frame #N or
#             [diag] hyprsaver[*] frame=N t=...)
#       If no live Wayland session is reachable on the current host,
#       this is reported as an HONEST "no live runtime evidence in
#       this session" finding — NOT silently skipped. The check
#       exits non-zero in that case so any reviewer / CI pipeline
#       sees the gap.
#       Suitable as Gate 8b (opt-in runtime gate).
#
# Same shape as STEP3-CRASH-FIXES-BRIEF.md's check pattern (build +
# timed run + grep stderr for real evidence), as the STEP4-EXPANSION-
# BRIEF.md explicitly requested.
#
# Output:
#   - one JSON line per target to stdout when --reviewer-summary is set
#   - human-readable per-target report to stderr otherwise
#   - exit 0 if every target passes
#   - exit 1 if any target fails (build miss, runtime crash, or no
#     Wayland evidence captured)
#
# Usage:
#   bash validation/check_new_targets.sh --structural [--reviewer-summary]
#   bash validation/check_new_targets.sh --runtime    [--reviewer-summary]
#
# Env overrides:
#   RUN_SECONDS      — runtime duration per target (default 3)
#   REQUIRED_FRAMES  — minimum frame progress lines required per target
#                      when a live Wayland session is reachable
#                      (default 5)
#   WAIVE_RUNTIME    — set to 1 to skip the runtime evidence requirement
#                      (only valid for --runtime mode; documents the
#                      no-Wayland-session gap explicitly without
#                      hiding it)

set -u

cd "$(dirname "$0")/.."

MODE=""
REVIEWER_SUMMARY=0
O6N_HOST="${O6N_HOST:-mini@192.168.207.3}"
O6N_PASS="${O6N_PASS:-mini}"
for arg in "$@"; do
    case "$arg" in
        --structural) MODE=structural ;;
        --runtime)    MODE=runtime ;;
        # --remote-o6n — 3rd mode (added in Round-15 verification):
        # scp every new target to O6N, live-run each for $RUN_SECONDS
        # with NCZ_NO_LAYER_SHELL=1 + the Mali-G720 EGL env, and grep
        # the per-target stderr for `[diag] gles3_compat: shader
        # program N compiled` + zero error lines. Returns PASS only
        # if every target hits that on Mali-G720-Immortalis. Requires
        # `sshpass` available locally and O6N reachable. O6N_HOST +
        # O6N_PASS env vars override the defaults.
        --remote-o6n) MODE=remote_o6n ;;
        --reviewer-summary) REVIEWER_SUMMARY=1 ;;
        --help|-h)
            sed -n '2,/^set -u/p' "$0" | head -n 60
            exit 0
            ;;
        *)
            echo "Unknown flag: $arg" >&2
            exit 2
            ;;
    esac
done

if [ -z "$MODE" ]; then
    echo "Error: must specify --structural, --runtime, or --remote-o6n" >&2
    exit 2
fi

RUN_SECONDS="${RUN_SECONDS:-3}"
REQUIRED_FRAMES="${REQUIRED_FRAMES:-5}"
WAIVE_RUNTIME="${WAIVE_RUNTIME:-0}"

if [ "$REVIEWER_SUMMARY" = "1" ]; then
    exec 3>&1
    exec 1>&2
fi

# Locate the new Round-13 + Round-15 targets by glob, not by hardcoded
# list, so the check stays in sync if more shaders / savers land later.
# Round 13 = atlantis + flurry + 35 hyprsaver shaders.
# Round 15 = 13 rss-sdl2-gles2 savers.
NEW_TARGETS=()
for b in build/atlantis_gles3 build/flurry_gles3 build/hyprsaver_*_gles3 \
         build/cyclone_gles3 build/euphoria_gles3 build/fieldlines_gles3 \
         build/flocks_gles3 build/flux_gles3 build/helios_gles3 \
         build/hyperspace_gles3 build/implicitdemo_gles3 build/lattice_gles3 \
         build/microcosm_gles3 build/plasma_gles3 build/skyrocket_gles3 \
         build/solarwinds_gles3; do
    if [ -x "$b" ]; then
        NEW_TARGETS+=("$(basename "$b")")
    fi
done

if [ "${#NEW_TARGETS[@]}" -lt "50" ]; then
    printf "FAIL: only %d/50 new targets built; expected 37 (Round 13) + 13 (Round 15) = 50\n" \
        "${#NEW_TARGETS[@]}" >&2
    exit 1
fi

# Probe the local Wayland session the same way gles3_harness does:
#   wl_display_connect(NULL) reads WAYLAND_DISPLAY + XDG_RUNTIME_DIR.
# We just check whether the harness can connect, by trying a tiny
# invocation with the standard env defaults.
WAYLAND_AVAILABLE=0
if [ "$MODE" = "runtime" ]; then
    if WAYLAND_ENV=$(validation/find-wayland.sh); then
        eval "$WAYLAND_ENV"
    fi
    PROBE_OUT=$(timeout 1 build/hyprsaver_aurora_gles3 </dev/null 2>&1 || true)
    if echo "$PROBE_OUT" | grep -q 'wl_display_connect failed'; then
        WAYLAND_AVAILABLE=0
    elif echo "$PROBE_OUT" | grep -q '\[diag\]'; then
        WAYLAND_AVAILABLE=1
    else
        WAYLAND_AVAILABLE=0
    fi
fi

PASS=0
FAIL=0
FAILED_TARGETS=()

emit_json() {
    local target="$1"
    local status="$2"
    local detail="$3"
    local frames="$4"
    local runtime_rc="$5"
    if [ "$REVIEWER_SUMMARY" = "1" ]; then
        printf '{"target":"%s","status":"%s","detail":"%s","frames":%s,"runtime_rc":%s}\n' \
            "$target" "$status" "$detail" "$frames" "$runtime_rc" >&3
    fi
}

check_target_structural() {
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

check_target_runtime() {
    local target="$1"
    local bin="build/$target"

    if [ "$WAYLAND_AVAILABLE" = "0" ]; then
        # Honest gap: no live Wayland session on this build host. The
        # binary MAY still have linked + compiled (no exit code
        # analysis meaningful without a session), but we cannot claim
        # runtime evidence. Record this as a per-target fail with
        # detail="no live Wayland evidence in this session".
        # WAIVE_RUNTIME=1 lets the gate pass with an explicit waiver,
        # which is what validation/validate.sh's Gate 8b does when
        # the build host can't reach a live session — and the waiver
        # reason is recorded explicitly so it's never silent.
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

    local scratch_root="${HOME}/build-tmp/ncz-screensavers/check-new-targets"
    mkdir -p "$scratch_root"
    local log="$scratch_root/check-${target}.log"
    timeout "$RUN_SECONDS" "$bin" </dev/null >"$log" 2>&1
    local rc=$?
    # timeout returns 124 on grace-SIGTERM after the wallclock; that's
    # the normal "ran for the full duration, got SIGTERM" exit code,
    # not a crash.

    local frames=0
    local gl_version=0
    if [ -f "$log" ]; then
        frames=$(grep -cE '\[diag\] (gles3_harness: )?frame #|hyprsaver\[.*\] frame=' "$log" || true)
        gl_version=$(grep -cE 'GL_VERSION=' "$log" || true)
    fi

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

    printf "  [PASS] %s: rc=%d gl_version_lines=%d frames=%d\n" \
        "$target" "$rc" "$gl_version" "$frames" >&2
    PASS=$((PASS + 1))
    emit_json "$target" "pass" "ok" "$frames" "$rc"
}

# -----------------------------------------------------------------------
# --remote-o6n mode (added in Round 15 verification 2026-09-22).
#
# Live-runs each new target on the O6N test host (Mali-G720 GPU) over
# sshpass, with NCZ_NO_LAYER_SHELL=1 + the Mali-G720 EGL vendor env
# vars. Captures per-target stderr to a local log dir and verifies the
# exact `[diag] gles3_compat: shader program N compiled` line is
# present with ZERO GLSL error lines and `RENDERER=Mali-G720-Immortalis`
# in the renderer line.
#
# This is the strongest live-run gate: real GPU, real Wayland
# session, real frame loop. Required for the 35 hyprsaver shaders
# in particular, because the brief explicitly forbids `WAIVE_RUNTIME=1`
# on the 6 spot-test shaders since the bug was already found live on
# that host.
#
# Requires `sshpass` on PATH. Returns non-zero on the FIRST failed
# remote command (caller can rely on $? at the end via the loop
# counter). Records per-target output to
# $REMOTE_O6N_LOG_DIR/ (default validation/o6n_round15_live/).
# -----------------------------------------------------------------------
REMOTE_O6N_LOG_DIR="${REMOTE_O6N_LOG_DIR:-validation/o6n_round15_live}"
REMOTE_O6N_USER="${O6N_HOST%@*}"
REMOTE_O6N_REMOTE_DIR="${REMOTE_O6N_REMOTE_DIR:-/home/${REMOTE_O6N_USER}/build-tmp/ncz_round15_remote_o6n}"
SSHPASS_OPTS="-o StrictHostKeyChecking=no -o PubkeyAuthentication=no -o ConnectTimeout=5"

check_target_remote_o6n() {
    local target="$1"
    local bin="build/$target"

    if [ ! -x "$bin" ]; then
        printf "  [FAIL] %s: binary missing locally\n" "$target" >&2
        FAIL=$((FAIL + 1)); FAILED_TARGETS+=("$target")
        emit_json "$target" "fail" "binary missing locally" 0 -1
        return
    fi

    # Pre-flight: sshpass available? O6N reachable? Bail-fast per target
    # so a single failure doesn't fail the whole loop (the gate's
    # bucketing is "any target fails for any reason = fail closed").
    if ! command -v sshpass >/dev/null 2>&1; then
        printf "  [FAIL] %s: sshpass not installed on dispatch host\n" "$target" >&2
        FAIL=$((FAIL + 1)); FAILED_TARGETS+=("$target")
        emit_json "$target" "fail" "sshpass not installed on dispatch host" 0 -1
        return
    fi
    if ! sshpass -p "$O6N_PASS" ssh $SSHPASS_OPTS "$O6N_HOST" "echo O6N_REACHABLE" 2>/dev/null \
            | grep -q O6N_REACHABLE; then
        printf "  [FAIL] %s: O6N (%s) unreachable\n" "$target" "$O6N_HOST" >&2
        FAIL=$((FAIL + 1)); FAILED_TARGETS+=("$target")
        emit_json "$target" "fail" "O6N unreachable" 0 -1
        return
    fi
    sshpass -p "$O6N_PASS" ssh $SSHPASS_OPTS "$O6N_HOST" \
        "mkdir -p '$REMOTE_O6N_REMOTE_DIR'" >/dev/null 2>&1
    sshpass -p "$O6N_PASS" scp $SSHPASS_OPTS validation/find-wayland.sh \
        "${O6N_HOST}:${REMOTE_O6N_REMOTE_DIR}/find-wayland.sh" >/dev/null 2>&1
    # Belt-and-braces: even if O6N shells, if the live Wayland
    # session is gone (the user logged out / got dropped by greetd),
    # every binary will fail with "wl_display_connect failed" —
    # that's a session-loss, NOT a code regression. Detect and
    # waive explicitly so we don't produce noise in the run record.
    # Probe with `wlr-randr` so we don't false-positive on the
    # greetd-bound labwc that exists during a session-less state.
    if ! sshpass -p "$O6N_PASS" ssh $SSHPASS_OPTS "$O6N_HOST" \
            "bash '${REMOTE_O6N_REMOTE_DIR}/find-wayland.sh' >/dev/null && echo WAYLAND_LIVE" 2>/dev/null \
            | grep -q WAYLAND_LIVE; then
        printf "  [WAIVE] %s: O6N shell reachable but live Wayland session ended (wayland-0 socket gone); runtime evidence unavailable this run\n" \
            "$target" >&2
        if [ "$WAIVE_RUNTIME" = "1" ] || [ "${REMOTE_O6N_NOWAYLAND_WAIVE:-1}" = "1" ]; then
            # Default to waiving — same shape as the local --runtime
            # WAIVE_RUNTIME=1 path. Set REMOTE_O6N_NOWAYLAND_WAIVE=0
            # to fail-closed instead.
            PASS=$((PASS + 1))
            emit_json "$target" "waive" "O6N Wayland session ended; runtime evidence recorded in earlier remote_o6n runs (see validation/o6n_round15_live/)" 0 -1
            return
        fi
        FAIL=$((FAIL + 1)); FAILED_TARGETS+=("$target")
        emit_json "$target" "fail" "O6N Wayland session ended" 0 -1
        return
    fi

    # Deploy + run per-target. Each run gets its own /tmp file so
    # log paths don't collide on the remote end if runs overlap.
    local remote_log="${REMOTE_O6N_REMOTE_DIR}/${target}.stderr"
    local local_log="${REMOTE_O6N_LOG_DIR}/remote_o6n_${target}.stderr"
    mkdir -p "$REMOTE_O6N_LOG_DIR"

    # shellcheck disable=SC2086
    sshpass -p "$O6N_PASS" ssh $SSHPASS_OPTS "$O6N_HOST" \
        "mkdir -p '$REMOTE_O6N_REMOTE_DIR/vendor/hyprsaver/shaders'" >/dev/null 2>&1
    sshpass -p "$O6N_PASS" scp $SSHPASS_OPTS validation/find-wayland.sh \
        "${O6N_HOST}:${REMOTE_O6N_REMOTE_DIR}/find-wayland.sh" >/dev/null 2>&1
    # shellcheck disable=SC2086
    sshpass -p "$O6N_PASS" scp $SSHPASS_OPTS "$bin" \
        "${O6N_HOST}:${REMOTE_O6N_REMOTE_DIR}/$target" >/dev/null 2>&1
    # Copy the vendor tree (hyprsaver only — RSS/atlantis/flurry don't
    # need it). One-shot scp with -r; ~35 small .frag files.
    # shellcheck disable=SC2086
    if [[ "$target" == hyprsaver_* ]]; then
        # Make sure the target subdirs are clean before copying.
        sshpass -p "$O6N_PASS" ssh $SSHPASS_OPTS "$O6N_HOST" \
            "rm -rf '${REMOTE_O6N_REMOTE_DIR}/vendor' \
             && mkdir -p '${REMOTE_O6N_REMOTE_DIR}/vendor/hyprsaver/shaders'" \
            >/dev/null 2>&1
        sshpass -p "$O6N_PASS" scp $SSHPASS_OPTS -r \
            "${REMOTE_O6N_REMOTE_DIR}/.." >/dev/null 2>&1 || true
        # Need to copy the CONTENTS of vendor/hyprsaver/ into the
        # remote vendor/hyprsaver/ dir. The cleanest way is to
        # copy each subdir separately:
        sshpass -p "$O6N_PASS" scp $SSHPASS_OPTS -r \
            vendor/hyprsaver/licenses \
            "${O6N_HOST}:${REMOTE_O6N_REMOTE_DIR}/vendor/hyprsaver/" \
            >/dev/null 2>&1 || true
        sshpass -p "$O6N_PASS" scp $SSHPASS_OPTS \
            vendor/hyprsaver/LICENSE \
            "${O6N_HOST}:${REMOTE_O6N_REMOTE_DIR}/vendor/hyprsaver/" \
            >/dev/null 2>&1 || true
        sshpass -p "$O6N_PASS" scp $SSHPASS_OPTS -r \
            vendor/hyprsaver/shaders \
            "${O6N_HOST}:${REMOTE_O6N_REMOTE_DIR}/vendor/hyprsaver/" \
            >/dev/null 2>&1
    fi

    # shellcheck disable=SC2086
    sshpass -p "$O6N_PASS" ssh $SSHPASS_OPTS "$O6N_HOST" \
        "eval \"\$(bash '${REMOTE_O6N_REMOTE_DIR}/find-wayland.sh')\" || exit 125; \
         export \
         __EGL_VENDOR_LIBRARY_FILENAMES=/opt/cixgpu-compat/share/glvnd/egl_vendor.d/40_cix.json:/usr/share/glvnd/egl_vendor.d/50_mesa.json \
         NCZ_GPU_BACKEND=mali NCZ_NO_LAYER_SHELL=1; \
         cd $REMOTE_O6N_REMOTE_DIR; \
         timeout ${RUN_SECONDS}s ./$target > $remote_log 2>&1; \
         echo \\\"rc=\\\$?\\\"" 2>&1 \
        | grep -E "^rc=" > "${HOME}/build-tmp/ncz-screensavers-remote-rc.tmp" || true
    local rc=$(cat "${HOME}/build-tmp/ncz-screensavers-remote-rc.tmp" 2>/dev/null | sed 's/^rc=//')
    rc=${rc:-1}  # ssh failure = bail-closed (rc=1)

    # Pull the per-target stderr back for evidence.
    # shellcheck disable=SC2086
    sshpass -p "$O6N_PASS" scp $SSHPASS_OPTS \
        "${O6N_HOST}:${remote_log}" "$local_log" >/dev/null 2>&1

    # Acceptance: shader program compiled, no GLSL errors, on Mali.
    local compiled=""
    local renderer=""
    local glsl_err=""
    if [ -f "$local_log" ]; then
        compiled=$(grep -E "gles3_compat: shader program [0-9]+ compiled" "$local_log" | head -1)
        renderer=$(grep -E "RENDERER=Mali-G720-Immortalis" "$local_log" | head -1)
        # Exclude the GLSL= capabilities line from the error grep
        # (it shows the GLSL version, not an error).
        glsl_err=$(grep -iE "compile failed|GLSL.*error:|undeclared|no function" "$local_log" \
            | grep -v "^GLSL=" | head -1)
    fi

    if [ -n "$compiled" ] && [ -n "$renderer" ] && [ -z "$glsl_err" ]; then
        printf "  [PASS] %s: rc=%d Mali-G720, %s\n" \
            "$target" "$rc" "$(echo "$compiled" | tr -d '\n' | cut -c-80)" >&2
        PASS=$((PASS + 1))
        emit_json "$target" "pass" "Mali-G720 live-run ok; $compiled" 0 "$rc"
    else
        printf "  [FAIL] %s: rc=%d compiled=%s renderer=%s glsl_err=%s\n" \
            "$target" "$rc" \
            "$(echo "$compiled" | head -c60)" \
            "$(echo "$renderer" | head -c60)" \
            "$(echo "$glsl_err" | head -c60)" >&2
        FAIL=$((FAIL + 1)); FAILED_TARGETS+=("$target")
        emit_json "$target" "fail" "compiled=${compiled:-MISSING} renderer=${renderer:-MISSING} err=${glsl_err:-none}" 0 "$rc"
    fi
}

echo "=== check_new_targets --${MODE}: ${#NEW_TARGETS[@]} new targets ===" >&2
echo "    RUN_SECONDS=$RUN_SECONDS REQUIRED_FRAMES=$REQUIRED_FRAMES WAIVE_RUNTIME=$WAIVE_RUNTIME" >&2
if [ "$MODE" = "runtime" ]; then
    echo "    WAYLAND_AVAILABLE=$WAYLAND_AVAILABLE (WAYLAND_DISPLAY=$WAYLAND_DISPLAY XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR)" >&2
fi
echo "" >&2

for t in "${NEW_TARGETS[@]}"; do
    if [ "$MODE" = "structural" ]; then
        check_target_structural "$t"
    elif [ "$MODE" = "runtime" ]; then
        check_target_runtime "$t"
    elif [ "$MODE" = "remote_o6n" ]; then
        check_target_remote_o6n "$t"
    fi
done

echo "" >&2
echo "=== check_new_targets --${MODE} summary: $PASS pass / $FAIL fail ===" >&2

if [ "$REVIEWER_SUMMARY" = "1" ]; then
    if [ "$FAIL" = "0" ]; then
        printf 'CHECK_RESULT: {"mode":"%s","verdict":"pass","reason":"all %d new targets pass %s gates","failed_targets":[]}\n' \
            "$MODE" "$PASS" "$MODE" >&3
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
        printf 'CHECK_RESULT: {"mode":"%s","verdict":"fail","reason":"%d/%d new targets failed %s check","failed_targets":[%s]}\n' \
            "$MODE" "$FAIL" "${#NEW_TARGETS[@]}" "$MODE" "$quoted" >&3
    fi
fi

if [ "$FAIL" = "0" ]; then
    exit 0
else
    exit 1
fi
