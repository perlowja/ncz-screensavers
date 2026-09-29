#!/usr/bin/env bash
# host-test.sh - drive the ncz-screensavers test agent on the NCZ-OS test hosts.
#
# Ships tools/host-test-agent.py (plus helpers and the package) to each host,
# runs it inside the user's real Wayland session, and pulls results.json,
# screenshots and logs back.  See docs/LAUNCHER-DESIGN.md and docs/HOST-TEST-RESULTS.md.
#
# Authentication is delegated: HOST_TEST_SSH is a command prefix invoked as
#   $HOST_TEST_SSH <alias> <remote command...>
# (default: plain ssh with BatchMode).  HT_SUDO_PW, when set, is forwarded to
# the agent through stdin only, never on a command line (the special value
# @user sends the host's login name, the convention on the lab test hosts).
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

declare -A HOST_USER=([chimera]=chimera [medusa]=medusa [pegasus]=pegasus [o6n]=mini)
declare -A HOST_IP=([chimera]=192.168.207.6 [medusa]=192.168.207.20 [pegasus]=192.168.207.85 [o6n]=192.168.207.3)
declare -A HOST_ARCH=([chimera]=amd64 [medusa]=amd64 [pegasus]=amd64 [o6n]=arm64)
ALL_HOSTS=(chimera medusa pegasus o6n)

hosts=()
deb_amd64=""
deb_arm64=""
seconds=8
hack_sel=all
phases="env,install,hacks,launcher,idle,color,chooser"
results=""
no_dpms=0
keep=0
dry=0

usage() {
    sed -n '2,12p' "$0"
    cat <<'EOF'

usage: host-test.sh [--host chimera|medusa|pegasus|o6n|all]... [--deb PATH]
       [--deb-amd64 PATH] [--deb-arm64 PATH] [--seconds N]
       [--hacks all|smoke|ID,ID] [--phases env,install,hacks,launcher,idle,color,chooser]
       [--results DIR] [--no-dpms] [--keep-installed] [--dry-run]
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --host) if [ "${2:-}" = all ]; then hosts+=("${ALL_HOSTS[@]}"); else hosts+=("${2:?}"); fi; shift 2 ;;
        --deb) deb_amd64="${2:?}"; deb_arm64="$2"; shift 2 ;;
        --deb-amd64) deb_amd64="${2:?}"; shift 2 ;;
        --deb-arm64) deb_arm64="${2:?}"; shift 2 ;;
        --seconds) seconds="${2:?}"; shift 2 ;;
        --hacks) hack_sel="${2:?}"; shift 2 ;;
        --phases) phases="${2:?}"; shift 2 ;;
        --results) results="${2:?}"; shift 2 ;;
        --no-dpms) no_dpms=1; shift ;;
        --keep-installed) keep=1; shift ;;
        --dry-run) dry=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done
[ ${#hosts[@]} -gt 0 ] || hosts=("${ALL_HOSTS[@]}")
[ -n "$results" ] || results="./host-test-results/$(date -u +%Y%m%dT%H%M%SZ)"

rsh() { # rsh <alias> <remote command...>
    local alias="$1"; shift
    if [ -n "${HOST_TEST_SSH:-}" ]; then
        # shellcheck disable=SC2086
        $HOST_TEST_SSH "$alias" "$@"
    else
        ssh -o BatchMode=yes -o StrictHostKeyChecking=accept-new -o ConnectTimeout=10 \
            "${HOST_USER[$alias]}@${HOST_IP[$alias]}" "$@"
    fi
}

run_host() {
    local h="$1" arch deb outdir stage
    if [ -z "${HOST_ARCH[$h]:-}" ]; then echo "unknown host $h" >&2; return 2; fi
    arch="${HOST_ARCH[$h]}"
    deb=""
    [ "$arch" = amd64 ] && deb="$deb_amd64"
    [ "$arch" = arm64 ] && deb="$deb_arm64"
    if [[ ",$phases," == *,install,* ]] && [ -z "$deb" ]; then
        echo "[$h] no .deb for $arch; the install phase will be skipped by the agent"
    fi
    outdir="$results/$h"
    if [ "$dry" = 1 ]; then
        echo "[$h] arch=$arch deb=${deb:-none} phases=$phases hacks=$hack_sel seconds=$seconds results=$outdir"
        return 0
    fi
    mkdir -p "$outdir"
    stage="$(mktemp -d)"
    cp "$HERE/host-test-agent.py" "$HERE/host_test_image.py" "$HERE/wl_poke.py" "$HERE/greetd_login.py" "$stage/"
    [ -n "$deb" ] && cp "$deb" "$stage/ncz-screensavers.deb"
    echo "[$h] shipping agent${deb:+ and package}"
    if ! rsh "$h" 'rm -rf "$HOME/ncz-host-test" && mkdir -p "$HOME/ncz-host-test" && tar -x -C "$HOME/ncz-host-test"' \
        < <(tar -C "$stage" -c .); then
        echo "[$h] FAIL: could not ship files" >&2
        rm -rf "$stage"
        return 1
    fi
    rm -rf "$stage"

    local args="--phases $phases --hacks $hack_sel --seconds $seconds --workdir \$HOME/ncz-host-test/results"
    [ -n "$deb" ] && args="$args --deb \$HOME/ncz-host-test/ncz-screensavers.deb"
    [ "$no_dpms" = 1 ] && args="$args --no-dpms"
    echo "[$h] running agent (phases=$phases)"
    # First stdin line carries the sudo secret (empty line = none).
    local pw="${HT_SUDO_PW:-}"
    [ "$pw" = "@user" ] && pw="${HOST_USER[$h]}"   # lab hosts: login name doubles as sudo secret
    printf '%s\n' "$pw" | rsh "$h" "read -r HT_SUDO_PW; export HT_SUDO_PW; cd \$HOME/ncz-host-test && python3 host-test-agent.py $args" \
        > "$outdir/agent.stdout" 2> "$outdir/agent.stderr"
    local rc=$?
    echo "[$h] agent exit=$rc; collecting results"
    rsh "$h" 'cd "$HOME/ncz-host-test/results" 2>/dev/null && tar -c .' 2>/dev/null | tar -x -C "$outdir" 2>/dev/null
    if [ "$keep" = 0 ] && [[ ",$phases," == *,install,* ]]; then
        : # the package stays installed: it is the product under test
    fi
    return "$rc"
}

overall=0
declare -A status
for h in "${hosts[@]}"; do
    run_host "$h"
    rc=$?
    status[$h]=$rc
    [ "$rc" -eq 0 ] || overall=1
done

echo
echo "host        result   detail"
for h in "${hosts[@]}"; do
    detail="-"
    js="$results/$h/results.json"
    if [ -f "$js" ]; then
        detail="$(python3 - "$js" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
s = d.get("summary", {})
print(f"pass={s.get('pass', 0)} fail={s.get('fail', 0)} skip={s.get('skip', 0)}")
PY
)"
    fi
    if [ "${status[$h]}" -eq 0 ]; then r=PASS; else r=FAIL; fi
    printf '%-11s %-8s %s\n' "$h" "$r" "$detail"
done
echo "results: $results"
exit "$overall"
