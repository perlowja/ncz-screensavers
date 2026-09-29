#!/usr/bin/env bash
# host-test.sh - drive the ncz-screensavers test agent on the NCZ-OS test hosts.
#
# Ships tools/host-test-agent.py (plus helpers and the package) to each host,
# runs it inside the user's real Wayland session, and pulls results.json,
# screenshots and logs back.  See docs/LAUNCHER-DESIGN.md and docs/HOST-TEST-RESULTS.md.
#
# Authentication is by password on every host (fleet policy: no dependence on ssh keys or an
# agent). The default transport is `sshpass -e ssh` with password authentication forced, so a
# missing or wrong password fails instead of silently using a key. The password comes from,
# in this order: HT_SSH_PW_<ALIAS> (alias upper case, - as _), SSHPASS, or a mode-0600 file
# named by HT_SSH_PWFILE (default ~/.ht-ssh-pw) holding "alias=password" lines or one bare
# password. The value @user means "the host's login name", the convention on the lab test
# hosts. It is handed to sshpass through the environment only, never on a command line.
# Host keys stay checked (accept-new): after a reinstall, verify the fingerprint once and
# update known_hosts. HOST_TEST_SSH, a command prefix invoked as
#   $HOST_TEST_SSH <alias> <remote command...>
# still replaces the whole transport. HT_SUDO_PW, when set, is forwarded to the agent
# through stdin only, never on a command line (@user as above).
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

declare -A HOST_USER=([chimera]=chimera [medusa]=medusa [pegasus]=pegasus [o6n]=mini [ms-r1]=mini)
declare -A HOST_IP=([chimera]=192.168.207.6 [medusa]=192.168.207.38 [pegasus]=192.168.207.85 [o6n]=192.168.207.3 [ms-r1]=192.168.207.66)
declare -A HOST_ARCH=([chimera]=amd64 [medusa]=amd64 [pegasus]=amd64 [o6n]=arm64 [ms-r1]=arm64)
ALL_HOSTS=(chimera medusa pegasus o6n ms-r1)

hosts=()
deb_amd64=""
deb_arm64=""
seconds=8
hack_sel=all
phases="env,install,hacks,launcher,idle,color,chooser"
results=""
no_dpms=0
gpu_offload=
allow_panthor=0
perf_mode=default
perf_seconds=10
perf_scale=default
keep=0
dry=0

usage() {
    sed -n '2,12p' "$0"
    cat <<'EOF'

usage: host-test.sh [--host chimera|medusa|pegasus|o6n|ms-r1|all]... [--deb PATH]
       [--deb-amd64 PATH] [--deb-arm64 PATH] [--seconds N]
       [--hacks all|smoke|ID,ID] [--phases env,install,hacks,launcher,idle,color,chooser]
       [--results DIR] [--no-dpms] [--gpu-offload off|prime|auto] [--keep-installed] [--dry-run]
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
        --perf-mode) perf_mode="${2:?}"; shift 2 ;;
        --perf-seconds) perf_seconds="${2:?}"; shift 2 ;;
        --perf-scale) perf_scale="${2:?}"; shift 2 ;;
        --allow-panthor) allow_panthor=1; shift ;;
        --gpu-offload) gpu_offload="${2:?}"; shift 2 ;;
        --keep-installed) keep=1; shift ;;
        --dry-run) dry=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done
[ ${#hosts[@]} -gt 0 ] || hosts=("${ALL_HOSTS[@]}")
[ -n "$results" ] || results="./host-test-results/$(date -u +%Y%m%dT%H%M%SZ)"

ssh_password() { # ssh_password <alias>: print the password source value for the host
    local a="$1" var v f line
    var="HT_SSH_PW_$(printf '%s' "$a" | tr 'a-z-' 'A-Z_')"
    v="${!var:-${SSHPASS:-}}"
    if [ -z "$v" ]; then
        f="${HT_SSH_PWFILE:-$HOME/.ht-ssh-pw}"
        if [ -r "$f" ]; then
            if [ "$(stat -c %a "$f" 2>/dev/null)" != 600 ]; then
                echo "host-test: $f must have mode 0600" >&2
                return 1
            fi
            line=$(grep -m1 "^$a=" "$f" | cut -d= -f2-)
            [ -n "$line" ] || line=$(grep -m1 -v '=' "$f")
            v="$line"
        fi
    fi
    [ "$v" = "@user" ] && v="${HOST_USER[$a]}"
    if [ -z "$v" ]; then
        echo "host-test: no ssh password for $a: set SSHPASS (or HT_SSH_PW_${a^^}), or HT_SSH_PWFILE; @user means the login name" >&2
        return 1
    fi
    printf '%s' "$v"
}

rsh() { # rsh <alias> <remote command...>
    local alias="$1"; shift
    if [ -n "${HOST_TEST_SSH:-}" ]; then
        # shellcheck disable=SC2086
        $HOST_TEST_SSH "$alias" "$@"
        return
    fi
    command -v sshpass >/dev/null 2>&1 || { echo "host-test: sshpass is not installed" >&2; return 127; }
    local pw
    pw=$(ssh_password "$alias") || return 1
    SSHPASS="$pw" sshpass -e ssh -o PreferredAuthentications=password -o PubkeyAuthentication=no \
        -o NumberOfPasswordPrompts=1 -o StrictHostKeyChecking=accept-new -o ConnectTimeout=10 \
        "${HOST_USER[$alias]}@${HOST_IP[$alias]}" "$@"
}

hostlock() { # hostlock <alias> <host-lock.sh args...>
    local a="$1"; shift
    local q
    q=$(printf '%q ' "$@")
    rsh "$a" "sh -s -- $q" < "$HERE/host-lock.sh"
}

# Wait (up to 3 minutes) for a quiet machine before starting; HOST_TEST_SKIP_LOAD=1 skips the check.
wait_quiet() {
    local a="$1" i load
    [ "${HOST_TEST_SKIP_LOAD:-0}" = 1 ] && return 0
    for i in $(seq 1 18); do
        load=$(rsh "$a" 'cut -d" " -f1 /proc/loadavg' 2>/dev/null | head -n1)
        awk -v l="${load:-0}" 'BEGIN { exit !(l < 2.0) }' && return 0
        sleep 10
    done
    echo "[$a] load stays at ${load:-?} (needs < 2); not starting (HOST_TEST_SKIP_LOAD=1 to override)" >&2
    return 1
}

remote_cleanup() { # stop anything this harness may have left running on the host
    # The agent runs in its own session; its pid is in agent.pid, so the whole group can be
    # stopped by pid (no name matching, which can hit the cleanup command itself).
    rsh "$1" 'ncz-screensaver stop >/dev/null 2>&1; f="$HOME/ncz-host-test/agent.pid"; if [ -r "$f" ]; then p=$(cat "$f"); kill -TERM -- "-$p" 2>/dev/null; sleep 1; kill -KILL -- "-$p" 2>/dev/null; rm -f "$f"; fi; true' >/dev/null 2>&1
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
    wait_quiet "$h" || return 1
    if ! hostlock "$h" acquire test "host-test.sh@$(hostname)" "phases=$phases" --expect 90 --wait "${HOST_TEST_LOCK_WAIT:-900}"; then
        echo "[$h] could not take the host lock; not starting" >&2
        return 1
    fi
    local refresher
    (while sleep 300; do hostlock "$h" refresh >/dev/null 2>&1; done) &
    refresher=$!
    trap 'kill $refresher 2>/dev/null; remote_cleanup "$h"; hostlock "$h" release >/dev/null 2>&1; exit 130' INT TERM
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
    [ -n "$gpu_offload" ] && args="$args --gpu-offload $gpu_offload"
    args="$args --perf-mode $perf_mode --perf-seconds $perf_seconds --perf-scale $perf_scale"
    [ "$allow_panthor" = 1 ] && args="$args --allow-panthor"
    echo "[$h] running agent (phases=$phases)"
    # First stdin line carries the sudo secret (empty line = none).
    local pw="${HT_SUDO_PW:-}"
    [ "$pw" = "@user" ] && pw="${HOST_USER[$h]}"   # lab hosts: login name doubles as sudo secret
    printf '%s\n' "$pw" | rsh "$h" "read -r HT_SUDO_PW; export HT_SUDO_PW; cd \$HOME/ncz-host-test && setsid -w sh -c 'echo \$\$ > \$HOME/ncz-host-test/agent.pid; exec timeout -k 30 ${HOST_TEST_TIMEOUT:-5400} python3 host-test-agent.py $args'" \
        > "$outdir/agent.stdout" 2> "$outdir/agent.stderr"
    local rc=$?
    echo "[$h] agent exit=$rc; collecting results"
    kill $refresher 2>/dev/null
    trap - INT TERM
    rsh "$h" 'cd "$HOME/ncz-host-test/results" 2>/dev/null && tar -c .' 2>/dev/null | tar -x -C "$outdir" 2>/dev/null
    remote_cleanup "$h"
    hostlock "$h" release >/dev/null 2>&1
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
