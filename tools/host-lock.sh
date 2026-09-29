#!/bin/sh
# host-lock.sh - exclusive lock for a shared test host (build or test work).
#
# usage: host-lock.sh acquire MODE WHO WHAT [--expect MINUTES] [--wait SECONDS] [--pid PID]
#        host-lock.sh refresh | release | status | wait [SECONDS]
#
# MODE is build or test. The lock is a directory created atomically with mkdir; its info file
# records mode, who, what, host, the optional owner pid (--pid, only meaningful for a process on this host), start, expected end and the last refresh. A holder that runs
# longer than a few minutes should call refresh. A lock whose owner process is gone (same host) or
# that has not been refreshed for 30 minutes is reported as STALE and never removed silently:
# release it explicitly with `release --force`.
#
# Lock path: $HOST_LOCK_DIR, default $HOME/HOST-LOCK.d. For the user "mini" (the O6N board) the
# alias $HOME/O6N-LOCK.d is kept as a symlink to it.
#
# exit status: 0 ok/free, 1 held (status), 2 wait timed out, 3 stale lock reported, 4 usage or not owner
set -u

LOCK=${HOST_LOCK_DIR:-$HOME/HOST-LOCK.d}
INFO=$LOCK/info
STALE_SECS=1800

now() { date +%s; }

field() { # field NAME -> value from the info file
    sed -n "s/^$1=//p" "$INFO" 2>/dev/null | head -n1
}

alias_link() {
    case "$(id -un)" in
        mini) [ -e "$HOME/O6N-LOCK.d" ] || ln -s "$LOCK" "$HOME/O6N-LOCK.d" 2>/dev/null ;;
    esac
}

is_stale() { # prints the reason and returns 0 when the lock is stale
    pid=$(field pid)
    host=$(field host)
    upd=$(field updated)
    if [ -n "$pid" ] && [ "$host" = "$(hostname)" ] && ! kill -0 "$pid" 2>/dev/null; then
        echo "owner process $pid is gone"
        return 0
    fi
    if [ -n "$upd" ] && [ $(($(now) - upd)) -gt "$STALE_SECS" ]; then
        echo "not refreshed for $((($(now) - upd) / 60)) minutes"
        return 0
    fi
    return 1
}

show() {
    printf 'HOLDER  %s (%s): %s\n' "$(field who)" "$(field mode)" "$(field what)"
    printf 'SINCE   %s, expected end %s, last refresh %s\n' \
        "$(date -d "@$(field start)" '+%F %T' 2>/dev/null)" \
        "$(date -d "@$(field expected_end)" '+%T' 2>/dev/null)" \
        "$(date -d "@$(field updated)" '+%T' 2>/dev/null)"
}

write_info() { # write_info START EXPECTED_END
    {
        echo "mode=$MODE"
        echo "who=$WHO"
        echo "what=$WHAT"
        echo "host=$(hostname)"
        echo "pid=${OWNER_PID:-}"
        echo "start=$1"
        echo "expected_end=$2"
        echo "updated=$(now)"
    } >"$INFO.tmp" && mv "$INFO.tmp" "$INFO"
}

cmd=${1:-}
[ $# -gt 0 ] && shift
case "$cmd" in
    acquire)
        MODE=${1:-}
        WHO=${2:-}
        WHAT=${3:-}
        [ $# -ge 3 ] || { echo "usage: $0 acquire MODE WHO WHAT [--expect MIN] [--wait SEC]" >&2; exit 4; }
        shift 3
        EXPECT=60
        WAIT=0
        OWNER_PID=
        while [ $# -gt 0 ]; do
            case "$1" in
                --expect) EXPECT=$2; shift 2 ;;
                --wait) WAIT=$2; shift 2 ;;
                --pid) OWNER_PID=$2; shift 2 ;;
                *) echo "unknown option $1" >&2; exit 4 ;;
            esac
        done
        case "$MODE" in build | test) ;; *) echo "MODE must be build or test" >&2; exit 4 ;; esac
        deadline=$(($(now) + WAIT))
        while :; do
            if mkdir "$LOCK" 2>/dev/null; then
                start=$(now)
                write_info "$start" $((start + EXPECT * 60))
                alias_link
                echo "acquired $LOCK ($MODE, $WHO)"
                exit 0
            fi
            if reason=$(is_stale); then
                echo "STALE lock at $LOCK ($reason); not removing it."
                show
                echo "If the owner is really gone: $0 release --force"
                exit 3
            fi
            if [ "$(now)" -ge "$deadline" ]; then
                echo "held:"
                show
                exit 2
            fi
            sleep 10
        done
        ;;
    refresh)
        [ -d "$LOCK" ] || { echo "no lock" >&2; exit 4; }
        MODE=$(field mode) WHO=$(field who) WHAT=$(field what) OWNER_PID=$(field pid)
        write_info "$(field start)" "$(field expected_end)"
        ;;
    release)
        [ -d "$LOCK" ] || { echo "no lock held"; exit 0; }
        if [ "${1:-}" != "--force" ] && [ "$(field host)" = "$(hostname)" ] && [ -n "${HOST_LOCK_WHO:-}" ] && [ "$(field who)" != "$HOST_LOCK_WHO" ]; then
            echo "held by $(field who), not $HOST_LOCK_WHO; use --force" >&2
            exit 4
        fi
        rm -rf "$LOCK"
        rm -f "$HOME/O6N-LOCK.d" 2>/dev/null
        echo "released"
        ;;
    status)
        if [ -d "$LOCK" ]; then
            show
            if reason=$(is_stale); then echo "STALE ($reason)"; fi
            printf 'LOAD    %s\n' "$(cut -d' ' -f1-3 /proc/loadavg)"
            exit 1
        fi
        printf 'free (load %s)\n' "$(cut -d' ' -f1-3 /proc/loadavg)"
        ;;
    wait)
        deadline=$(($(now) + ${1:-600}))
        while [ -d "$LOCK" ]; do
            if reason=$(is_stale); then echo "STALE lock ($reason)"; show; exit 3; fi
            [ "$(now)" -ge "$deadline" ] && { echo "still held"; show; exit 2; }
            sleep 10
        done
        echo "free"
        ;;
    *)
        sed -n '2,17p' "$0"
        exit 4
        ;;
esac
