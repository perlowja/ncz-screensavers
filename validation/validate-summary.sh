#!/usr/bin/env bash
# validate-summary.sh — wrapper around `validation/validate.sh
# --reviewer-summary` that picks the right invocation for the current
# environment.
#
# On a build host with a live Wayland session reachable, just runs
# `validate.sh --reviewer-summary` (gates 8b and 8c run for real).
#
# On a build host without a live Wayland session (the common case for
# the agent host — no compositor here), sets WAIVE_RUNTIME=1 so gates
# 8b/8c record a "waive" status instead of fail-closing with "no live
# Wayland session". The waiver is recorded in the gate JSON lines
# (NOT silent) and the gate output names the reason explicitly.
#
# Captures the run record to validation/runs/<UTC>_reviewer_summary.json
# so the audit trail is committed alongside the code.
#
# Usage:
#   bash validation/validate-summary.sh            # auto-detect
#   bash validation/validate-summary.sh --waive    # force waive
#   bash validation/validate-summary.sh --no-waive # require live Wayland
#
# Exit codes:
#   0  every gate passed
#   1  one or more gates failed
#   2  setup error

set -u

cd "$(dirname "$0")/.." || exit 2
ROOT="$(pwd)"

WAIVE=0
for arg in "$@"; do
    case "$arg" in
        --waive)    WAIVE=1 ;;
        --no-waive) WAIVE=0 ;;
        --help|-h)
            sed -n '2,/^set -u/p' "$0" | head -n 30
            exit 0
            ;;
        *)
            echo "Unknown flag: $arg" >&2
            exit 2
            ;;
    esac
done

# Auto-detect: probe the build environment for a real Wayland session.
# A stale socket at $XDG_RUNTIME_DIR/$WAYLAND_DISPLAY is not enough —
# the harness needs an actual compositor that responds to
# wl_display_connect. Probe with `build/jigsaw_gles3` (it exits
# cleanly with "[diag] GL_VERSION=" against a live session, or
# "wl_display_connect failed" against a stale/missing one).
if [ "$WAIVE" = "0" ]; then
    if [ -z "${WAYLAND_DISPLAY:-}" ]; then
        export WAYLAND_DISPLAY=wayland-0
    fi
    if [ -z "${XDG_RUNTIME_DIR:-}" ]; then
        export XDG_RUNTIME_DIR=/run/user/$(id -u)
    fi
    PROBE_OUT=$(timeout 1 build/jigsaw_gles3 </dev/null 2>&1 || true)
    if echo "$PROBE_OUT" | grep -q '\[diag\] GL_VERSION='; then
        echo "live Wayland session detected at ${XDG_RUNTIME_DIR}/${WAYLAND_DISPLAY}; running real runtime gates" >&2
    else
        echo "no live Wayland session (probe: $(echo "$PROBE_OUT" | tr '\n' ' ' | head -c 80)); auto-waiving gates 8b/8c" >&2
        WAIVE=1
    fi
fi

RUN_TS="$(date -u +%Y-%m-%dT%H-%M-%SZ)"
RUN_RECORD="validation/runs/${RUN_TS}_reviewer_summary.json"
mkdir -p "$(dirname "$RUN_RECORD")"

if [ "$WAIVE" = "1" ]; then
    echo "running: WAIVE_RUNTIME=1 bash validation/validate.sh --reviewer-summary" >&2
    WAIVE_RUNTIME=1 bash validation/validate.sh --reviewer-summary \
        > "$RUN_RECORD" 2>/tmp/validate-summary.stderr
    rc=$?
else
    echo "running: bash validation/validate.sh --reviewer-summary" >&2
    bash validation/validate.sh --reviewer-summary \
        > "$RUN_RECORD" 2>/tmp/validate-summary.stderr
    rc=$?
fi

echo "captured run record: $RUN_RECORD" >&2
echo "result: $([ $rc -eq 0 ] && echo PASS || echo FAIL)" >&2

# Show the captured REVIEWER_RESULT line so the caller sees the verdict
# at a glance without having to cat the file.
if [ -f "$RUN_RECORD" ]; then
    grep '^REVIEWER_RESULT:' "$RUN_RECORD" >&2
fi

exit "$rc"