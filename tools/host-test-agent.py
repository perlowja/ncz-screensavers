#!/usr/bin/python3
"""host-test-agent - runs INSIDE a Wayland session on a build host.

The controller (``tools/host-test.sh``) ships this script, ``wl_poke.py``,
and the catalog to ``~/ncz-host-test/`` on each host and invokes it with
the phases to run. Every measurement in the harness happens here; the
controller only ships code and collects tarballs of the results.

Phases (selected by the controller via ``--phases``):

  env       - read host facts: hostname, kernel, GPU, compositor, globals
  install   - install a .deb and verify package state (skipped if no --deb)
  hacks     - the gate: launch every selected catalog hack and confirm it
              draws a non-black, animated frame on top of the baseline
  launcher  - exercise the launcher CLI: list/start/status/stop/set-* and
              the crash-fallback / PDEATHSIG / playlist rotations
  idle      - drive ncz-screensaver-idled via ext-idle-notify-v1: motion
              and key dismissal, idle inhibition, lock chain, DPMS
  color     - run blackhole_gles3 in three color modes and confirm they
              actually look different on screen
  chooser   - exercise ncz-screensaver-settings (dump + self-test + GUI)

The agent writes ``results.json`` plus ``shots/`` and ``logs/`` under its
working directory. ``--selftest`` exercises the PPM helpers on synthetic
frames and exits; it never needs a real Wayland session.
"""

from __future__ import annotations

import argparse
import contextlib
import dataclasses
import datetime as _dt
import glob
import json
import os
import pathlib
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
from collections.abc import Iterable, Sequence
from typing import Any

# Local helper module (sibling of this file).
import host_test_image as hti

# ---------------------------------------------------------------------------
# Constants and small helpers.
# ---------------------------------------------------------------------------

# Hard timeouts for every subprocess we spawn. A crashing/hung hack must not
# freeze the harness.
SUBPROC_DEFAULT_TIMEOUT = 15.0
GRIM_TIMEOUT = 10.0
PREVIEW_EXTRA_SECONDS = 10.0  # run a preview this much longer than --seconds
IDLE_DEADLINE_HEADROOM = 6.0  # extra time we allow over the configured delay
DISMISS_DEADLINE = 4.0  # max time a hack may take to dismiss on input

# Vars we copy out of the compositor's environ into every child we spawn.
# A bare ssh shell inherits none of these and EGL falls back to llvmpipe,
# which is why the design mandates re-reading /proc/<labwc>/environ.
COMPOSITOR_ENV_PREFIXES = (
    "LD_LIBRARY_PATH",
    "__EGL",
    "MESA_",
    "LIBGL_",
    "GBM_",
    "VK_",
    "NCZ_GPU",
)

# Catalog knobs.
CATALOG_PATH_DEFAULT = "/usr/share/ncz-screensavers/hacks.tsv"
CATALOG_FALLBACK = "/usr/share/ncz-screensaver-chooser/hacks.tsv"
SCHEMA_ID = "dev.ncz.screensaver"
SYSTEM_SCHEMA_DIR = "/usr/share/glib-2.0/schemas"

# Hosts we expect to talk to. Used only for documentation; the controller
# doesn't ship this list to the agent.
HOST_FACTS = {
    "chimera": {"ip": "192.168.207.6", "user": "chimera", "arch": "x86_64"},
    "medusa": {"ip": "192.168.207.20", "user": "medusa", "arch": "x86_64"},
    "pegasus": {"ip": "192.168.207.85", "user": "pegasus", "arch": "x86_64"},
    "o6n": {"ip": "192.168.207.3", "user": "mini", "arch": "aarch64"},
}

# Gates defined by the design brief for a passing hack measurement.
COVERAGE_GATE = 0.15
TILE_GATE = 0.25  # sparse-scene rule: lit 8x8 tiles, plus motion, plus not black
MOTION_GATE = 0.005
BASELINE_DIFF_GATE = 0.10

# Color-phase gates.
COLOR_COVERAGE_GATE = 0.02
COLOR_CHROMA_MIN = 0.02
COLOR_BRIGHTNESS_MIN = 1.10  # 10% difference required if chroma is below gate

# Hack binary directory.
HACK_BIN_DIR = "/usr/lib/ncz-screensavers"

# Runtime files written by idled.
IDLED_STATE = "ncz-screensaver/idled.json"
HACK_LOG = "ncz-screensaver/hack.log"
SUPERVISOR_STATE = "ncz-screensaver/state.json"


def _now_iso() -> str:
    """UTC timestamp in ISO-8601 with second resolution."""
    return _dt.datetime.now(_dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def _read_sudo_secret() -> str | None:
    """Read the sudo password piped in via the controller.

    The controller forwards ``HT_SUDO_PW`` to the agent through the wrapper
    stdin; we strip it from ``os.environ`` immediately so it never appears
    in any captured subprocess environ. ``None`` means "use sudo -n".
    """
    pw = os.environ.pop("HT_SUDO_PW", None)
    if pw is None or pw == "":
        return None
    # Clear a few well-known passes through env vars as a belt-and-braces
    # measure; the caller passes only this one.
    return pw


def _sudo_prefix(pw: str | None) -> list[str]:
    """Build a ``sudo`` argv prefix that reads the password from stdin."""
    if pw is None:
        return ["sudo", "-n"]
    # ``-S`` reads the password from stdin; the agent pipes it via the
    # wrapper, never via the command line.
    return ["sudo", "-S", "-p", ""]


def _run_with_sudo(
    cmd: list[str],
    *,
    pw: str | None,
    timeout: float,
    env: dict[str, str] | None = None,
    cwd: str | None = None,
    stdin_text: str | None = None,
    check: bool = False,
) -> subprocess.CompletedProcess[str]:
    """Run a command, prefixing ``sudo`` if needed.

    The sudo password is fed via stdin only when present; without one we
    fall back to ``sudo -n`` which fails fast on hosts without NOPASSWD.
    """
    argv = _sudo_prefix(pw) + cmd
    proc = subprocess.run(
        argv,
        capture_output=True,
        text=True,
        timeout=timeout,
        env=env,
        cwd=cwd,
        input=(pw + "\n" + (stdin_text or "")) if pw is not None else stdin_text,
        check=check,
    )
    return proc


def _log(text: str) -> None:
    """Append a timestamped line to ``agent.log`` (best-effort)."""
    log_path = os.environ.get("HT_AGENT_LOG")
    if not log_path:
        return
    try:
        with open(log_path, "a", encoding="utf-8") as fh:
            fh.write(f"[{_now_iso()}] {text}\n")
    except OSError:
        pass


# ---------------------------------------------------------------------------
# Data classes for the recorded checks.
# ---------------------------------------------------------------------------


@dataclasses.dataclass
class Check:
    """A single check recorded into ``results.json``.

    Fields map directly to the brief: ``name`` and ``status`` are the keys
    used by the markdown generator, ``detail`` is a one-line summary,
    ``evidence`` is a path relative to the host's result directory, and
    ``metrics`` carries the numeric measurements (coverage, motion, ...).
    """

    name: str
    status: str  # "pass" | "fail" | "skip"
    detail: str = ""
    evidence: str | None = None
    metrics: dict[str, Any] = dataclasses.field(default_factory=dict)

    def to_dict(self) -> dict[str, Any]:
        out: dict[str, Any] = {
            "name": self.name,
            "status": self.status,
            "detail": self.detail,
            "evidence": self.evidence,
            "metrics": self.metrics,
        }
        return out


def _record(
    checks: list[Check],
    name: str,
    status: str,
    detail: str = "",
    evidence: str | None = None,
    metrics: dict[str, Any] | None = None,
) -> Check:
    """Append a Check and return it (handy for tail assertions)."""
    c = Check(
        name=name,
        status=status,
        detail=detail,
        evidence=evidence,
        metrics=metrics or {},
    )
    checks.append(c)
    return c


# ---------------------------------------------------------------------------
# Environment discovery.
# ---------------------------------------------------------------------------


def _compositor_env() -> dict[str, str]:
    """Read the running compositor's environment from ``/proc``.

    Scans processes owned by the current uid looking for one whose argv[0]
    ends in ``labwc`` (the only compositor on the build hosts per the
    design). Returns the GPU-relevant vars as a dict, ready to splat into
    every child's ``environ``.
    """
    uid = os.getuid()
    for entry in sorted(
        pathlib.Path("/proc").iterdir(),
        key=lambda p: int(p.name) if p.name.isdigit() else 0,
    ):
        if not entry.name.isdigit():
            continue
        try:
            status = (entry / "status").read_text()
            uid_line = next(
                (ln for ln in status.splitlines() if ln.startswith("Uid:")), ""
            )
            parts = uid_line.split()
            if len(parts) < 2 or parts[1] != str(uid):
                continue
            cmdline = (entry / "cmdline").read_bytes().split(b"\x00")
            comm = (entry / "comm").read_text().strip()
            argv0 = (cmdline[0].decode() if cmdline and cmdline[0] else "").split("/")[
                -1
            ]
            if not (comm == "labwc" or argv0 == "labwc"):
                continue
            env_bytes = (entry / "environ").read_bytes()
            env: dict[str, str] = {}
            for chunk in env_bytes.split(b"\x00"):
                if not chunk:
                    continue
                try:
                    line = chunk.decode("utf-8")
                except UnicodeDecodeError:
                    continue
                if "=" not in line:
                    continue
                k, v = line.split("=", 1)
                if any(k.startswith(prefix) for prefix in COMPOSITOR_ENV_PREFIXES):
                    env[k] = v
            return env
        except (OSError, PermissionError, ValueError):
            continue
    return {}


def _build_session_env() -> dict[str, str]:
    """Return the merged environment every spawned subprocess inherits.

    Order: the agent's own environ (lowest priority), then the compositor's
    GPU vars (highest priority for graphics-related keys).
    """
    base = dict(os.environ)
    base.pop("HT_SUDO_PW", None)
    gpu = _compositor_env()
    # Splat the compositor vars; the design says they always win.
    base.update(gpu)
    rt = base.setdefault("XDG_RUNTIME_DIR", f"/run/user/{os.getuid()}")
    base.setdefault("WAYLAND_DISPLAY", "wayland-0")
    base.setdefault("DBUS_SESSION_BUS_ADDRESS", f"unix:path={rt}/bus")
    base["G_MESSAGES_DEBUG"] = ""
    return base


# ---------------------------------------------------------------------------
# Catalog parsing.
# ---------------------------------------------------------------------------


def _read_catalog(paths: Sequence[str]) -> dict[str, dict[str, str]]:
    """Read a TSV catalog of ``id<TAB>title<TAB>group<TAB>group``.

    Returns ``{id: {"title": ..., "group": ...}}``. Unknown rows and blank
    lines are skipped; this is forgiving on purpose.
    """
    catalog: dict[str, dict[str, str]] = {}
    for path in paths:
        try:
            with open(path, "r", encoding="utf-8") as f:
                for line in f:
                    if not line.strip() or line.lstrip().startswith("#"):
                        continue
                    parts = line.rstrip("\n").split("\t")
                    if len(parts) < 2:
                        continue
                    hack_id = parts[0].strip()
                    if not hack_id:
                        continue
                    title = parts[1].strip()
                    group = parts[2].strip() if len(parts) > 2 else ""
                    catalog[hack_id] = {"title": title, "group": group}
        except OSError:
            continue
    return catalog


# ---------------------------------------------------------------------------
# Subprocess helpers.
# ---------------------------------------------------------------------------


def _run(
    cmd: list[str],
    *,
    timeout: float = SUBPROC_DEFAULT_TIMEOUT,
    env: dict[str, str] | None = None,
    cwd: str | None = None,
    check: bool = False,
    input_bytes: bytes | None = None,
) -> subprocess.CompletedProcess:
    """Run a subprocess with hard timeout, no PTY, and output captured."""
    return subprocess.run(
        cmd,
        capture_output=True,
        timeout=timeout,
        env=env,
        cwd=cwd,
        input=input_bytes,
        check=check,
    )


def _capture_grim(path: str, scale: float, env: dict[str, str]) -> tuple[bool, str]:
    """Run ``grim -t ppm -s <scale> -`` and write the result to ``path``.

    Returns ``(ok, detail)``. ``detail`` carries either the success message
    or the stderr text on failure. Scale is sent as a string so callers
    can use fractions like ``0.125``.
    """
    try:
        proc = _run(
            ["grim", "-t", "ppm", "-s", f"{scale:g}", "-"],
            timeout=GRIM_TIMEOUT,
            env=env,
        )
    except subprocess.TimeoutExpired:
        return False, "grim timed out"
    except FileNotFoundError:
        return False, "grim not installed"
    except OSError as exc:
        return False, f"grim exec failed: {exc}"
    if proc.returncode != 0:
        return False, proc.stderr.decode("utf-8", errors="replace")[:400]
    try:
        pathlib.Path(path).write_bytes(proc.stdout)
    except OSError as exc:
        return False, f"write {path}: {exc}"
    return True, f"wrote {len(proc.stdout)} bytes"


def _screenshot_to_png(
    ppm_path: str, png_path: str, env: dict[str, str], scale: float
) -> tuple[bool, str]:
    """Run ``grim -s <scale> -`` and save as PNG at ``png_path``."""
    try:
        proc = _run(
            ["grim", "-s", f"{scale:g}", png_path],
            timeout=GRIM_TIMEOUT,
            env=env,
        )
    except subprocess.TimeoutExpired:
        return False, "grim png timed out"
    except FileNotFoundError:
        return False, "grim not installed"
    if proc.returncode != 0:
        return False, proc.stderr.decode("utf-8", errors="replace")[:400]
    if not os.path.exists(png_path):
        return False, "grim returned 0 but no PNG was created"
    # ppm_path is not used directly; the brief asks for the PNG anyway.
    _ = ppm_path
    return True, f"wrote {png_path}"


# ---------------------------------------------------------------------------
# Phase: env.
# ---------------------------------------------------------------------------


def _read_os_release() -> dict[str, str]:
    out: dict[str, str] = {}
    try:
        with open("/etc/os-release", "r", encoding="utf-8") as fh:
            for line in fh:
                if "=" in line and not line.lstrip().startswith("#"):
                    k, _, v = line.rstrip().partition("=")
                    out[k] = v.strip('"')
    except OSError:
        pass
    return out


def _read_proc(cmd: str, args: list[str]) -> str:
    try:
        proc = _run([cmd, *args], timeout=5.0)
    except (subprocess.TimeoutExpired, FileNotFoundError, OSError):
        return ""
    if proc.returncode != 0:
        return ""
    return proc.stdout.decode("utf-8", errors="replace")


def _lspci_display() -> str:
    """Return ``lspci -nnk`` output for the display class (0300)."""
    raw = _read_proc("lspci", ["-nnk", "-d", "::0300"])
    if raw:
        return raw.strip()
    return _read_proc("lspci", ["-nnk"])


def _driver_in_use() -> str:
    """Kernel drivers bound to the display-class PCI devices (comma list)."""
    raw = _read_proc("lspci", ["-nnk", "-d", "::0300"])
    drivers = []
    for line in raw.splitlines():
        line = line.strip()
        if line.startswith("Kernel driver in use:"):
            drivers.append(line.split(":", 1)[1].strip())
    if not drivers:
        # Platform GPUs (for example the Sky1 Mali) have no PCI display device;
        # read the DRM class devices instead.
        for card in sorted(glob.glob("/sys/class/drm/card[0-9]")):
            link = os.path.join(card, "device", "driver")
            if os.path.islink(link):
                drivers.append(
                    f"{os.path.basename(os.readlink(link))} ({os.path.basename(card)})"
                )
    return ", ".join(drivers)


def _egl_info(env: dict[str, str]) -> dict[str, str]:
    """GLES renderer and version as seen through EGL on the Wayland session."""
    out: dict[str, str] = {}
    for cmd in (["eglinfo", "-p", "wayland", "-B"], ["eglinfo", "-B"]):
        try:
            proc = _run(cmd, timeout=10.0, env=env)
        except (FileNotFoundError, OSError, subprocess.TimeoutExpired):
            continue
        if proc.returncode != 0:
            continue
        for line in proc.stdout.decode("utf-8", errors="replace").splitlines():
            for key in ("renderer", "version", "vendor"):
                pre = f"OpenGL ES profile {key}:"
                if line.startswith(pre):
                    out[f"gles_{key}"] = line[len(pre) :].strip()
        if out:
            out["source"] = " ".join(cmd)
            return out
    return out


def _gl_renderer_from_hack(hack_path: str, env: dict[str, str]) -> dict[str, str]:
    """Last-ditch GL renderer discovery: run the smallest hack briefly
    and read its stderr. Returns ``{renderer: "...", version: "..."}``."""
    out: dict[str, str] = {}
    try:
        proc = _run([hack_path], timeout=4.0, env=env, input_bytes=b"")
    except (FileNotFoundError, OSError, subprocess.TimeoutExpired):
        return out
    text = proc.stderr.decode("utf-8", errors="replace")
    for line in text.splitlines():
        if "[diag]" in line and "GL renderer" in line:
            # Format: "[diag] GL renderer: <name> version: <ver>"
            rest = line.split("[diag]", 1)[1].strip()
            if ":" in rest:
                _, payload = rest.split(":", 1)
                parts = [p.strip() for p in payload.split("version")]
                if len(parts) >= 1:
                    out["renderer"] = parts[0]
                if len(parts) >= 2:
                    out["version"] = parts[1]
            break
    return out


def _dpkg_versions(packages: Iterable[str]) -> dict[str, str]:
    """Return ``{pkg: version}`` for the given package names."""
    out: dict[str, str] = {}
    pkgs = list(packages)
    if not pkgs:
        return out
    try:
        proc = _run(
            ["dpkg-query", "-W", "-f=${Package}\t${Version}\n", *pkgs], timeout=10.0
        )
    except (FileNotFoundError, OSError, subprocess.TimeoutExpired):
        return out
    for line in proc.stdout.decode("utf-8", errors="replace").splitlines():
        if "\t" not in line:
            continue
        name, _, ver = line.partition("\t")
        out[name] = ver
    return out


def _compositor_pid() -> tuple[str, int]:
    """Return ``(name, pid)`` for the active compositor or ``(, 0)``."""
    uid = os.getuid()
    for entry in pathlib.Path("/proc").iterdir():
        if not entry.name.isdigit():
            continue
        try:
            status = (entry / "status").read_text()
            uid_line = next(
                (ln for ln in status.splitlines() if ln.startswith("Uid:")), ""
            )
            parts = uid_line.split()
            if len(parts) < 2 or parts[1] != str(uid):
                continue
            comm = (entry / "comm").read_text().strip()
            if comm in {"labwc", "sway", "river", "hyprland", "kwin_wayland"}:
                return comm, int(entry.name)
        except (OSError, ValueError):
            continue
    return "", 0


def _wlr_randr() -> str:
    return _read_proc("wlr-randr", [])


def _wl_globals() -> list[dict[str, Any]]:
    """Run ``wl_poke.py globals`` and return parsed JSON list."""
    script = os.environ.get("HT_WL_POKE") or os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "wl_poke.py"
    )
    if not script:
        return []
    try:
        proc = _run([sys.executable, script, "globals"], timeout=5.0)
    except (FileNotFoundError, OSError, subprocess.TimeoutExpired):
        return []
    if proc.returncode != 0:
        return []
    try:
        return json.loads(proc.stdout.decode("utf-8", errors="replace"))
    except json.JSONDecodeError:
        return []


def _read_unit_state(unit: str, env: dict[str, str]) -> str:
    """Return ``systemctl --user is-active <unit>`` output."""
    try:
        proc = _run(
            ["systemctl", "--user", "is-active", unit],
            timeout=5.0,
            env=env,
        )
    except (FileNotFoundError, OSError, subprocess.TimeoutExpired):
        return ""
    return proc.stdout.decode("utf-8", errors="replace").strip()


def _guard(fn):
    """Turn an unexpected exception in a phase into a recorded failure."""

    def inner(results, checks, *args, **kwargs):
        try:
            fn(results, checks, *args, **kwargs)
        except Exception:  # noqa: BLE001 - a phase crash must not lose the run
            import traceback

            _record(
                checks, f"{fn.__name__}-crash", "fail", traceback.format_exc()[-800:]
            )

    inner.__name__ = fn.__name__
    return inner


_EVDEV = {c: k for k, c in zip("qwertyuiop", range(16, 26))}
_EVDEV.update({c: k for k, c in zip("asdfghjkl", range(30, 39))})
_EVDEV.update({c: k for k, c in zip("zxcvbnm", range(44, 51))})
_EVDEV.update({c: k for k, c in zip("1234567890", range(2, 12))})


def _try_unlock(env, pw):
    """Unlock a locked test session by typing the account secret through a
    virtual keyboard. The secret only travels as evdev codes, never logged."""
    if not pw or any(ch not in _EVDEV for ch in pw):
        return False
    codes = ",".join([str(_EVDEV[ch]) for ch in pw] + ["28"])
    _poke(env, "motion")
    time.sleep(1.0)
    _poke(env, "key", "--codes", codes)
    return _wait_until(lambda: not _is_display_locked(), 8.0) is not None


@_guard
def phase_env(
    results: dict[str, Any], checks: list[Check], env: dict[str, str]
) -> None:
    """Record host facts into ``results`` and the env-phase ``checks``."""
    gpu: dict[str, Any] = {}

    if _is_display_locked():
        ok = _try_unlock(env, results.get("_pw"))
        _record(
            checks,
            "session-unlocked",
            "pass" if ok else "fail",
            "session was locked; unlocked with the virtual keyboard"
            if ok
            else "session is locked and could not be unlocked; timing tests need an unlocked session",
        )
    else:
        _record(checks, "session-unlocked", "pass", "session is not locked")

    name, pid = _compositor_pid()
    if name:
        results["compositor"] = {"name": name, "pid": pid}
        _record(checks, f"compositor-{name}", "pass", f"pid={pid}")
    else:
        results["compositor"] = {"name": "", "pid": 0}
        _record(checks, "compositor", "fail", "no compositor found in /proc")

    pcie = _lspci_display()
    gpu["lspci"] = pcie
    if pcie:
        _record(checks, "lspci", "pass", "lspci -nnk -d ::0300 returned data")
    else:
        _record(checks, "lspci", "fail", "lspci returned no display devices")

    driver = _driver_in_use()
    if driver:
        gpu["driver"] = driver
        _record(checks, "driver", "pass", f"kernel driver(s): {driver}")
    else:
        _record(checks, "driver", "fail", "no kernel driver bound to display class")

    egl = _egl_info(env)
    gpu["egl"] = egl
    if egl:
        _record(
            checks,
            "egl-info",
            "pass",
            f"{egl.get('gles_renderer', '?')} | {egl.get('gles_version', '?')}",
        )
    else:
        # Fall back to running a hack briefly.
        hack = os.path.join(HACK_BIN_DIR, "blackhole_gles3")
        if os.path.exists(hack):
            gl = _gl_renderer_from_hack(hack, env)
            if gl:
                gpu["gl"] = gl
                _record(checks, "egl-info", "pass", "fallback: hack stderr GL renderer")
            else:
                _record(
                    checks, "egl-info", "skip", "no EGL tool, hack stderr uninformative"
                )
        else:
            _record(checks, "egl-info", "skip", "no EGL tool and no blackhole_gles3")

    results["gpu"] = gpu
    pkgs = _dpkg_versions(
        [
            "libgl1-mesa-dri",
            "libegl-mesa0",
            "libgbm1",
            "libvulkan1",
            "mesa-vulkan-drivers",
            "ncz-screensavers",
        ]
    )
    results["package"] = pkgs
    if "ncz-screensavers" in pkgs:
        _record(
            checks,
            "package-ncz-screensavers",
            "pass",
            f"installed {pkgs['ncz-screensavers']}",
        )
    else:
        _record(
            checks,
            "package-ncz-screensavers",
            "fail",
            "ncz-screensavers package not installed",
        )

    globals_ = _wl_globals()
    results["wayland_globals"] = [f"{g['interface']} v{g['version']}" for g in globals_]
    if globals_:
        _record(checks, "wl-registry", "pass", f"{len(globals_)} globals advertised")
        expected = [
            "ext_idle_notifier_v1",
            "zwp_idle_inhibit_manager_v1",
            "ext_session_lock_manager_v1",
            "zwlr_layer_shell_v1",
            "zwlr_output_power_manager_v1",
            "zwlr_screencopy_manager_v1",
            "ext_image_copy_capture_manager_v1",
            "zwlr_virtual_pointer_manager_v1",
            "zwp_virtual_keyboard_manager_v1",
        ]
        present = {g["interface"] for g in globals_}
        for iface in expected:
            if iface in present:
                _record(checks, f"global-{iface}", "pass", "advertised")
            else:
                _record(checks, f"global-{iface}", "fail", "not advertised")
    else:
        _record(checks, "wl-registry", "fail", "wl_poke.py globals returned no data")

    randr = _wlr_randr().strip()
    results["outputs"] = [randr] if randr else []
    if randr:
        _record(checks, "wlr-randr", "pass", randr.splitlines()[0])
    else:
        _record(checks, "wlr-randr", "fail", "no output from wlr-randr")

    for svc in ("ncz-screensaver-idled.service", "ncz-idle-manager.service"):
        state = _read_unit_state(svc, env)
        results.setdefault("services", {})[svc] = state
        if state == "active":
            # Informational: the harness stops the older idle daemon while it
            # runs and restores it afterwards.
            _record(checks, f"service-{svc}", "pass", f"active before the run: {svc}")
        elif state in ("inactive", "failed", ""):
            _record(checks, f"service-{svc}", "pass", state or "absent")
        else:
            _record(checks, f"service-{svc}", "skip", f"unexpected state: {state}")

    swayidle = _read_proc("pgrep", ["-x", "swayidle"])
    if swayidle.strip():
        _record(
            checks,
            "swayidle",
            "pass",
            "swayidle was running (older idle manager); paused during the run",
        )
    else:
        _record(checks, "swayidle", "pass", "no swayidle process")


# ---------------------------------------------------------------------------
# Phase: install.
# ---------------------------------------------------------------------------


@_guard
def phase_install(
    results: dict[str, Any],
    checks: list[Check],
    env: dict[str, str],
    deb_path: str | None,
    pw: str | None,
    workdir: str,
) -> None:
    if not deb_path:
        _record(checks, "install-skip", "skip", "no --deb given")
        return
    deb_basename = os.path.basename(deb_path)
    remote_deb = os.path.join(workdir, deb_basename)
    try:
        shutil.copyfile(deb_path, remote_deb)
    except OSError as exc:
        _record(
            checks, "install-copy", "fail", f"copy {deb_path} -> {remote_deb}: {exc}"
        )
        return
    _record(checks, "install-copy", "pass", f"copied to {remote_deb}")

    # Step 1: dry-run apt-get install -s to detect removals.
    try:
        proc = _run(
            ["apt-get", "install", "-s", "--reinstall", remote_deb],
            timeout=60.0,
            env=env,
        )
    except (FileNotFoundError, OSError, subprocess.TimeoutExpired) as exc:
        _record(checks, "install-simulate", "fail", f"apt-get -s failed: {exc}")
        return
    text = proc.stdout.decode("utf-8", errors="replace")
    remv = [ln for ln in text.splitlines() if ln.startswith("Remv ")]
    inst = [ln for ln in text.splitlines() if ln.startswith("Inst ")]
    metrics = {"removals": len(remv), "installs": len(inst)}
    if remv:
        _record(
            checks,
            "install-removals",
            "fail",
            f"{len(remv)} packages would be removed: {remv[:3]}",
            metrics=metrics,
        )
        return
    _record(
        checks,
        "install-removals",
        "pass",
        f"no removals; {len(inst)} installs",
        metrics=metrics,
    )

    # Step 2: real install with sudo.
    try:
        proc = _run_with_sudo(
            [
                "env",
                "DEBIAN_FRONTEND=noninteractive",
                "apt-get",
                "install",
                "-y",
                "--reinstall",
                remote_deb,
            ],
            pw=pw,
            timeout=180.0,
            env=env,
        )
    except subprocess.TimeoutExpired:
        _record(checks, "install-real", "fail", "apt-get install timed out")
        return
    except OSError as exc:
        _record(checks, "install-real", "fail", f"apt-get install failed: {exc}")
        return
    if proc.returncode != 0:
        _record(
            checks,
            "install-real",
            "fail",
            proc.stderr[:400],
        )
        return
    _record(checks, "install-real", "pass", "apt-get install succeeded")

    # Verify post-install state.
    try:
        proc = _run(["dpkg", "-s", "ncz-screensavers"], timeout=10.0, env=env)
    except (FileNotFoundError, OSError, subprocess.TimeoutExpired) as exc:
        _record(checks, "install-verify", "fail", f"dpkg -s failed: {exc}")
        return
    text = proc.stdout.decode("utf-8", errors="replace")
    ok = "Status: install ok installed" in text
    if not ok:
        _record(
            checks,
            "install-verify",
            "fail",
            text.splitlines()[0] if text else "no dpkg output",
        )
        return
    _record(checks, "install-verify", "pass", text.splitlines()[0])

    must_exist = [
        "/usr/bin/ncz-screensaver",
        "/usr/libexec/ncz-screensaver-idled",
        os.path.join(HACK_BIN_DIR, "blackhole_gles3"),
        "/usr/bin/ncz-screensaver-settings",
    ]
    for path in must_exist:
        if os.path.exists(path):
            _record(checks, f"file-{os.path.basename(path)}", "pass", path)
        else:
            _record(
                checks, f"file-{os.path.basename(path)}", "fail", f"missing: {path}"
            )

    # gsettings schema compiled?
    try:
        proc = _run(
            ["gsettings", "list-keys", SCHEMA_ID],
            timeout=5.0,
            env=env,
        )
    except (FileNotFoundError, OSError, subprocess.TimeoutExpired) as exc:
        _record(checks, "schema-keys", "fail", f"gsettings failed: {exc}")
    else:
        keys = proc.stdout.decode("utf-8").split()
        if keys:
            _record(
                checks,
                "schema-keys",
                "pass",
                f"{len(keys)} keys: {', '.join(sorted(keys))}",
            )
        else:
            _record(checks, "schema-keys", "fail", "gsettings returned no keys")

    installed_hacks = (
        sorted(p.name for p in pathlib.Path(HACK_BIN_DIR).glob("*_gles3"))
        if os.path.isdir(HACK_BIN_DIR)
        else []
    )
    catalog_paths = [
        os.environ.get("HT_CATALOG", CATALOG_PATH_DEFAULT),
        CATALOG_FALLBACK,
    ]
    catalog = _read_catalog(catalog_paths)
    catalog_ids = sorted(catalog.keys())
    installed_ids = sorted(pathlib.Path(p).stem for p in installed_hacks)
    metrics = {
        "installed_hacks": len(installed_ids),
        "catalog_hacks": len(catalog_ids),
    }
    if len(installed_ids) >= 80:
        _record(
            checks,
            "hacks-installed",
            "pass",
            f"{len(installed_ids)}/{len(catalog_ids)} catalog hacks present",
            metrics=metrics,
        )
    else:
        _record(
            checks,
            "hacks-installed",
            "fail",
            f"only {len(installed_ids)}/{len(catalog_ids)} catalog hacks installed",
            metrics=metrics,
        )


# ---------------------------------------------------------------------------
# Phase: hacks.
# ---------------------------------------------------------------------------


def _is_display_locked() -> bool:
    """Heuristic: singularity-lockscreen runs as the locking client."""
    try:
        proc = _run(["pgrep", "-af", "singularity-lockscreen"], timeout=3.0)
    except (FileNotFoundError, OSError, subprocess.TimeoutExpired):
        return False
    return bool(proc.stdout.strip())


def _measure_coverage_motion(
    ppm_a_path: str, ppm_b_path: str, baseline_path: str
) -> dict[str, float]:
    """Parse three PPM frames and compute the metrics required by the gate."""
    out: dict[str, float] = {}
    try:
        _, _, a_buf = hti.parse_ppm(pathlib.Path(ppm_a_path).read_bytes())
        bw, bh, b_buf = hti.parse_ppm(pathlib.Path(ppm_b_path).read_bytes())
        _, _, base_buf = hti.parse_ppm(pathlib.Path(baseline_path).read_bytes())
    except (OSError, hti.PPMError) as exc:
        out["error"] = str(exc)
        return out
    out["coverage"] = hti.coverage_fraction(b_buf)
    out["tile_coverage"] = hti.tile_coverage(bw, bh, b_buf)
    out["motion"] = hti.frame_diff_fraction(a_buf, b_buf)
    out["baseline_diff"] = hti.frame_diff_fraction(base_buf, b_buf)
    return out


def _hack_alive(binary: str) -> bool:
    """Return True if any process matches ``binary`` by exact path."""
    try:
        proc = _run(["pgrep", "-f", binary], timeout=3.0)
    except (FileNotFoundError, OSError, subprocess.TimeoutExpired):
        return False
    return bool(proc.stdout.strip())


def _stop_with_launcher(launcher_path: str, env: dict[str, str]) -> tuple[bool, str]:
    try:
        proc = _run([launcher_path, "stop"], timeout=8.0, env=env)
    except (FileNotFoundError, OSError, subprocess.TimeoutExpired) as exc:
        return False, f"launcher stop: {exc}"
    if proc.returncode not in (0, 3):
        return False, proc.stderr.decode("utf-8", errors="replace")[:200]
    return True, "ok"


def _display_probe(env):
    """True when a known bright hack is visible in a screenshot."""
    _cli(env, "preview", "voronoi_gles3", "--seconds", "12")
    _wait_until(lambda: _status(env).get("running"), 5.0)
    time.sleep(2.5)
    frame = _grab_frame(env)
    _cli(env, "stop")
    _wait_until(lambda: not _status(env).get("running"), 5.0)
    return frame is not None and hti.coverage_fraction(frame[2]) > 0.5


def _recover_display(env, pw):
    """A session lock whose client died leaves the screen black with no lock
    process running. Start a fresh lock client (the protocol lets it take over)
    and unlock it through the virtual keyboard."""
    locker = "/opt/singularity/bin/singularity-lockscreen"
    if not os.path.exists(locker):
        return False
    if not _is_display_locked():
        subprocess.Popen(
            [locker],
            env=env,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            stdin=subprocess.DEVNULL,
            start_new_session=True,
        )
        time.sleep(5.0)
    return _try_unlock(env, pw)


@_guard
def phase_hacks(
    results: dict[str, Any],
    checks: list[Check],
    env: dict[str, str],
    workdir: str,
    hack_ids: list[str],
    seconds: float,
    launcher_path: str,
) -> None:
    shots_dir = os.path.join(workdir, "shots")
    logs_dir = os.path.join(workdir, "logs")
    os.makedirs(shots_dir, exist_ok=True)
    os.makedirs(logs_dir, exist_ok=True)

    launcher_available = os.path.exists(launcher_path)
    if not launcher_available:
        _record(
            checks,
            "launcher-available",
            "fail",
            f"{launcher_path} missing; falling back to direct exec",
        )
    else:
        _record(checks, "launcher-available", "pass", launcher_path)

    catalog_paths = [
        os.environ.get("HT_CATALOG", CATALOG_PATH_DEFAULT),
        CATALOG_FALLBACK,
    ]
    catalog = _read_catalog(catalog_paths)

    if os.path.exists(os.path.join(HACK_BIN_DIR, "voronoi_gles3")):
        alive = _display_probe(env)
        if not alive:
            recovered = _recover_display(env, results.get("_pw")) and _display_probe(
                env
            )
            _record(
                checks,
                "display-alive",
                "pass" if recovered else "fail",
                "display was blank (orphaned session lock); recovered with a fresh lock client and unlock"
                if recovered
                else "display shows nothing even with a bright hack running; hack results would be meaningless",
            )
            if not recovered:
                return
        else:
            _record(
                checks,
                "display-alive",
                "pass",
                "a probe hack is visible before the run",
            )

    baseline_path = os.path.join(shots_dir, "_baseline.ppm")
    ok, detail = _capture_grim(baseline_path, 0.125, env)
    if not ok:
        _record(checks, "baseline-capture", "fail", detail)
        return
    _record(checks, "baseline-capture", "pass", detail)

    if _is_display_locked():
        _record(
            checks,
            "display-locked",
            "fail",
            "singularity-lockscreen is running; aborting hacks phase",
        )
        return

    hack_results: list[dict[str, Any]] = []
    for hid in hack_ids:
        if _is_display_locked():
            _record(
                checks,
                f"hack-{hid}",
                "fail",
                "display became locked mid-run",
            )
            break

        binary = os.path.join(HACK_BIN_DIR, hid)
        if not os.path.exists(binary):
            res = _record(
                checks,
                f"hack-{hid}",
                "fail",
                f"binary not installed: {binary}",
                metrics={"installed": False},
            )
            hack_results.append(
                {
                    "id": hid,
                    "title": catalog.get(hid, {}).get("title", ""),
                    "group": catalog.get(hid, {}).get("group", ""),
                    "status": "fail",
                    "detail": res.detail,
                    "metrics": {},
                    "evidence": None,
                }
            )
            continue

        # Start the hack. Prefer the launcher (it sets the right env); fall
        # back to running the binary in its own process group.
        started_via = "launcher"
        proc_hack: subprocess.Popen | None = None
        if launcher_available:
            try:
                proc_hack = subprocess.Popen(
                    [
                        launcher_path,
                        "preview",
                        hid,
                        "--seconds",
                        str(int(seconds + PREVIEW_EXTRA_SECONDS)),
                    ],
                    env=env,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                    start_new_session=True,
                )
            except (FileNotFoundError, OSError) as exc:
                proc_hack = None
                _record(
                    checks,
                    f"launch-{hid}",
                    "fail",
                    f"launcher preview failed: {exc}; falling back",
                )
        if proc_hack is None:
            started_via = "direct"
            try:
                proc_hack = subprocess.Popen(
                    [binary],
                    env=env,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                    start_new_session=True,
                )
            except (FileNotFoundError, OSError) as exc:
                _record(
                    checks,
                    f"hack-{hid}",
                    "fail",
                    f"failed to exec {binary}: {exc}",
                )
                hack_results.append(
                    {
                        "id": hid,
                        "title": catalog.get(hid, {}).get("title", ""),
                        "group": catalog.get(hid, {}).get("group", ""),
                        "status": "fail",
                        "detail": str(exc),
                        "metrics": {},
                        "evidence": None,
                    }
                )
                continue

        # Give the hack a moment to map its layer surface and render.
        time.sleep(2.5)
        frame_a = os.path.join(shots_dir, f"{hid}_a.ppm")
        ok_a, detail_a = _capture_grim(frame_a, 0.125, env)
        if not ok_a:
            _record(checks, f"hack-{hid}-frameA", "fail", detail_a)
        time.sleep(1.5)
        frame_b = os.path.join(shots_dir, f"{hid}_b.ppm")
        png_path = os.path.join(shots_dir, f"{hid}.png")
        ok_b, _ = _capture_grim(frame_b, 0.125, env)
        png_ok, _ = _screenshot_to_png(frame_b, png_path, env, 0.25)

        alive = _hack_alive(binary)
        metrics = (
            _measure_coverage_motion(frame_a, frame_b, baseline_path)
            if (ok_a and ok_b)
            else {}
        )
        evidence_short = os.path.basename(png_path) if png_ok else None

        # Stop the hack. Prefer the launcher so it tears down its supervisor
        # cleanly; the direct fallback uses SIGTERM on the process group.
        t_stop = time.monotonic()
        if started_via == "launcher":
            _stop_with_launcher(launcher_path, env)
        else:
            with contextlib.suppress(ProcessLookupError):
                os.killpg(os.getpgid(proc_hack.pid), signal.SIGTERM)
            try:
                proc_hack.wait(timeout=3.0)
            except subprocess.TimeoutExpired:
                with contextlib.suppress(ProcessLookupError):
                    os.killpg(os.getpgid(proc_hack.pid), signal.SIGKILL)

        # Confirm no leftover.
        deadline = time.monotonic() + 3.0
        while time.monotonic() < deadline and _hack_alive(binary):
            time.sleep(0.2)

        leftover = _hack_alive(binary)
        stop_seconds = time.monotonic() - t_stop

        sub: dict[str, Any] = {
            "process_alive": alive,
            "leftover": leftover,
            "started_via": started_via,
            "frame_a_ok": ok_a,
            "frame_b_ok": ok_b,
            "png_ok": png_ok,
            "stop_seconds": round(stop_seconds, 2),
        }
        sub.update(metrics)

        # Documented rule: a hack passes when it is healthy and moving and
        # either fills at least 15% of the pixels (the operator's gate) or,
        # for line-art scenes on black, lights at least 25% of the 8x8 tiles.
        # Hacks that pass only through the tile rule are flagged "sparse".
        healthy = ok_b and png_ok and alive and not leftover
        moving = metrics.get("motion", 0.0) >= MOTION_GATE
        dense = metrics.get("coverage", 0.0) >= COVERAGE_GATE
        tiles = metrics.get("tile_coverage", 0.0) >= TILE_GATE
        passed = healthy and moving and (dense or tiles)
        sub["sparse"] = bool(passed and not dense)
        status = "pass" if passed else "fail"
        detail = (
            f"coverage={metrics.get('coverage', 0):.3f} "
            f"motion={metrics.get('motion', 0):.4f} "
            f"baseline_diff={metrics.get('baseline_diff', 0):.3f} "
            f"alive={alive} leftover={leftover} "
            f"tiles={metrics.get('tile_coverage', 0):.2f} stop={stop_seconds:.2f}s"
        )
        if sub["sparse"]:
            detail += " SPARSE (passes via the tile rule only)"
        if not passed:
            # Pull the last 20 lines of the hack log if we have it.
            log = os.path.join("/run/user", str(os.getuid()), HACK_LOG)
            if os.path.exists(log):
                try:
                    tail = (
                        pathlib.Path(log)
                        .read_text(encoding="utf-8", errors="replace")
                        .splitlines()
                    )
                    tail_text = "\n".join(tail[-20:])
                    log_dst = os.path.join(logs_dir, f"{hid}.txt")
                    pathlib.Path(log_dst).write_text(tail_text + "\n", encoding="utf-8")
                    detail += f"; log={os.path.basename(log_dst)}"
                except OSError as exc:
                    detail += f"; log-read-failed: {exc}"
        _record(
            checks,
            f"hack-{hid}",
            status,
            detail,
            evidence=evidence_short,
            metrics=sub,
        )
        hack_results.append(
            {
                "id": hid,
                "title": catalog.get(hid, {}).get("title", ""),
                "group": catalog.get(hid, {}).get("group", ""),
                "status": status,
                "detail": detail,
                "metrics": sub,
                "evidence": evidence_short,
            }
        )

        time.sleep(0.5)

    results["hacks"] = hack_results


# ---------------------------------------------------------------------------
# Shared helpers for the launcher / idle / color / chooser phases.
# ---------------------------------------------------------------------------

LAUNCHER = "/usr/bin/ncz-screensaver"
IDLED = "/usr/libexec/ncz-screensaver-idled"
SETTINGS_APP = "/usr/bin/ncz-screensaver-settings"
IDLED_UNIT = "ncz-screensaver-idled.service"
OLD_IDLE_UNIT = "ncz-idle-manager.service"


def _rt_dir() -> str:
    return os.environ.get("XDG_RUNTIME_DIR") or f"/run/user/{os.getuid()}"


def _isolated_env(env: dict[str, str], extra: dict[str, str] | None = None):
    """Session env plus a private keyfile GSettings backend (no dconf writes)."""
    tmp = tempfile.mkdtemp(prefix="ncz-ht-cfg-")
    out = dict(env)
    out["XDG_CONFIG_HOME"] = os.path.join(tmp, "config")
    out["XDG_STATE_HOME"] = os.path.join(tmp, "state")
    out["GSETTINGS_BACKEND"] = "keyfile"
    out["G_MESSAGES_DEBUG"] = ""
    os.makedirs(out["XDG_CONFIG_HOME"], exist_ok=True)
    if extra:
        out.update(extra)
    return out


def _cli(env, *args, timeout=15.0):
    """Run the launcher CLI; returns CompletedProcess with text output."""
    try:
        return subprocess.run(
            [LAUNCHER, *args],
            capture_output=True,
            text=True,
            timeout=timeout,
            env=env,
            check=False,
        )
    except subprocess.TimeoutExpired as exc:
        return subprocess.CompletedProcess(args, 124, "", f"timeout: {exc}")
    except OSError as exc:
        return subprocess.CompletedProcess(args, 127, "", str(exc))


def _status(env):
    """Launcher status as a dict (running False when not running)."""
    p = _cli(env, "status", "--json", timeout=8.0)
    try:
        return json.loads(p.stdout)
    except ValueError:
        return {"running": False, "error": p.stderr[:200]}


def _wait_until(pred, timeout, step=0.2):
    """Poll pred() until truthy; returns seconds waited or None on timeout."""
    t0 = time.monotonic()
    while time.monotonic() - t0 < timeout:
        if pred():
            return time.monotonic() - t0
        time.sleep(step)
    return None


def _grab_frame(env, scale=0.125):
    """Screenshot as (w, h, rgb) or None."""
    fd, path = tempfile.mkstemp(suffix=".ppm", prefix="ncz-ht-")
    os.close(fd)
    try:
        ok, _ = _capture_grim(path, scale, env)
        if not ok:
            return None
        return hti.parse_ppm(pathlib.Path(path).read_bytes())
    except (OSError, hti.PPMError):
        return None
    finally:
        with contextlib.suppress(OSError):
            os.unlink(path)


def _kill_hacks():
    """Belt and braces between phases: nothing of ours may keep running."""
    for pat in ("ncz-screensaver.*--foreground", "/usr/lib/ncz-screensavers/"):
        subprocess.run(["pkill", "-f", pat], capture_output=True, check=False)
    time.sleep(0.5)


def _pids(pattern):
    p = subprocess.run(
        ["pgrep", "-f", pattern], capture_output=True, text=True, check=False
    )
    return [int(x) for x in p.stdout.split() if x.isdigit() and int(x) != os.getpid()]


def _unit_active(env, unit):
    p = subprocess.run(
        ["systemctl", "--user", "is-active", unit],
        capture_output=True,
        text=True,
        env=env,
        check=False,
    )
    return p.stdout.strip() == "active"


def _systemctl(env, *args):
    return subprocess.run(
        ["systemctl", "--user", *args],
        capture_output=True,
        text=True,
        timeout=20,
        env=env,
        check=False,
    )


def _idled_json():
    try:
        return json.loads(pathlib.Path(_rt_dir(), IDLED_STATE).read_text())
    except (OSError, ValueError):
        return {}


def _poke(env, *args, timeout=10.0):
    script = os.environ.get("HT_WL_POKE") or os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "wl_poke.py"
    )
    try:
        return subprocess.run(
            [sys.executable, script, *args],
            capture_output=True,
            text=True,
            timeout=timeout,
            env=env,
            check=False,
        )
    except subprocess.TimeoutExpired:
        return subprocess.CompletedProcess(args, 124, "", "timeout")


# ---------------------------------------------------------------------------
# Phase: launcher.
# ---------------------------------------------------------------------------


@_guard
def phase_launcher(results, checks, env, workdir, launcher_path):
    if not os.path.exists(LAUNCHER):
        _record(checks, "launcher-installed", "fail", f"{LAUNCHER} missing")
        return
    ienv = _isolated_env(env)
    _cli(ienv, "stop")
    _kill_hacks()

    p = _cli(ienv, "list", "--json", "--installed")
    try:
        n_installed = len(json.loads(p.stdout))
    except ValueError:
        n_installed = -1
    _record(
        checks,
        "list-installed",
        "pass" if n_installed >= 80 else "fail",
        f"{n_installed} installed hacks in the catalog",
    )

    # Lifecycle: start, status, single instance, stop.
    p = _cli(ienv, "start", "--hack", "voronoi_gles3", "--seconds", "60")
    up = _wait_until(lambda: _status(ienv).get("running"), 5.0)
    _record(
        checks,
        "start-status",
        "pass" if up is not None else "fail",
        f"running after {up:.1f}s"
        if up is not None
        else f"not running: {p.stderr[:200]}",
    )
    p2 = _cli(ienv, "start", "--hack", "voronoi_gles3", "--seconds", "60")
    sup = _pids("ncz-screensaver.*--foreground")
    _record(
        checks,
        "single-instance",
        "pass" if "already running" in p2.stdout and len(sup) == 1 else "fail",
        f"second start said {p2.stdout.strip()!r}; supervisors={len(sup)}",
    )
    # The compositor's graphics environment must reach the hack.
    st = _status(ienv)
    child_env = {}
    with contextlib.suppress(OSError, ValueError, TypeError):
        state = json.loads(
            pathlib.Path(_rt_dir(), "ncz-screensaver", "state.json").read_text()
        )
        raw = pathlib.Path(f"/proc/{state['child_pid']}/environ").read_bytes()
        child_env = dict(
            x.split("=", 1)
            for x in raw.decode(errors="replace").split("\0")
            if "=" in x
        )
    comp = _compositor_env()
    missing = [
        k
        for k in comp
        if k.startswith(("__EGL", "MESA_", "NCZ_GPU")) and k not in child_env
    ]
    _record(
        checks,
        "child-gpu-env",
        "pass" if child_env.get("WAYLAND_DISPLAY") and not missing else "fail",
        f"WAYLAND_DISPLAY={child_env.get('WAYLAND_DISPLAY')!r}, compositor GPU vars missing in child: {missing}",
    )
    _cli(ienv, "stop")
    gone = _wait_until(
        lambda: (
            not _status(ienv).get("running") and not _pids("/usr/lib/ncz-screensavers/")
        ),
        5.0,
    )
    rc = _cli(ienv, "status").returncode
    _record(
        checks,
        "stop",
        "pass" if gone is not None and rc == 3 else "fail",
        f"status rc={rc}, leftover hacks={_pids('/usr/lib/ncz-screensavers/')} ({st.get('hack')})",
    )

    # Crash fallback and playlist, using fake hacks next to real ones.
    fake = tempfile.mkdtemp(prefix="ncz-ht-fake-")
    crash = os.path.join(fake, "fakecrash")
    pathlib.Path(crash).write_text("#!/bin/sh\nexit 1\n")
    os.chmod(crash, 0o755)
    with contextlib.suppress(OSError):
        os.symlink(f"{HACK_BIN_DIR}/voronoi_gles3", os.path.join(fake, "fakereal"))
        os.symlink(f"{HACK_BIN_DIR}/klein_gles3", os.path.join(fake, "fakereal2"))
    fenv = _isolated_env(
        env,
        {
            "NCZ_SCREENSAVER_DIRS": fake,
            "NCZ_SCREENSAVER_ALLOW_UNLISTED": "1",
            "NCZ_SCREENSAVER_MIN_CYCLE": "1",
        },
    )
    _cli(fenv, "config", "set", "verify-render", "false")
    _cli(fenv, "config", "set", "playlist", "fakecrash,fakereal")
    _cli(fenv, "set-mode", "playlist")
    subprocess.Popen(
        [LAUNCHER, "start", "--force", "--seconds", "25"],
        env=fenv,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    ok = _wait_until(lambda: _status(fenv).get("hack") == "fakereal", 12.0)
    _record(
        checks,
        "crash-fallback",
        "pass" if ok is not None else "fail",
        "playlist skipped the crashing hack and ran the real one"
        if ok is not None
        else f"status={_status(fenv)}",
    )
    _cli(fenv, "stop")
    _wait_until(lambda: not _status(fenv).get("running"), 5.0)

    _cli(fenv, "config", "set", "playlist", "fakereal,fakereal2")
    _cli(fenv, "config", "set", "cycle-delay", "5")
    subprocess.Popen(
        [LAUNCHER, "start", "--force", "--seconds", "30"],
        env=fenv,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    seen: list[str] = []

    def _watch():
        h = _status(fenv).get("hack")
        if h and (not seen or seen[-1] != h):
            seen.append(h)
        return len(seen) >= 3

    _wait_until(_watch, 24.0, step=0.5)
    _record(
        checks,
        "playlist-rotation",
        "pass" if seen[:3] == ["fakereal", "fakereal2", "fakereal"] else "fail",
        f"observed order {seen}",
    )
    _cli(fenv, "stop")
    _wait_until(lambda: not _status(fenv).get("running"), 5.0)

    # Config round trips through the CLI.
    for setter, key, val in (
        (["set-timeout", "42"], "hack-idle-delay", 42),
        (["set-mode", "random"], "mode", "random"),
        (["set-hack", "klein_gles3"], "hack-id", "klein_gles3"),
        (["set-color", "kipthorne"], "blackhole-color-mode", "kipthorne"),
    ):
        _cli(ienv, *setter)
        got = _cli(ienv, "config", "get", key).stdout.strip()
        try:
            got = json.loads(got)
        except ValueError:
            pass
        _record(
            checks,
            f"config-{setter[0]}",
            "pass" if got == val else "fail",
            f"{key}={got!r} expected {val!r}",
        )

    # kill -9 of the supervisor must not orphan the hack.
    _cli(ienv, "start", "--hack", "voronoi_gles3", "--seconds", "60")
    _wait_until(lambda: _status(ienv).get("running"), 5.0)
    try:
        state = json.loads(
            pathlib.Path(_rt_dir(), "ncz-screensaver", "state.json").read_text()
        )
        os.kill(state["pid"], signal.SIGKILL)
        gone = _wait_until(
            lambda: not _pids("/usr/lib/ncz-screensavers/voronoi_gles3"), 6.0
        )
        _record(
            checks,
            "pdeathsig",
            "pass" if gone is not None else "fail",
            "hack exited after the supervisor was killed"
            if gone is not None
            else "hack orphaned",
        )
    except (OSError, ValueError, KeyError) as exc:
        _record(checks, "pdeathsig", "fail", f"no state to kill: {exc}")
    _cli(ienv, "stop")
    _kill_hacks()

    # Informational: a hack forced onto the NVIDIA EGL vendor (only when the
    # driver is already loaded; nothing is installed and the variable is set
    # for this one process only, never ambiently).
    nv_json = "/usr/share/glvnd/egl_vendor.d/10_nvidia.json"
    if (
        os.path.exists(nv_json)
        and "nvidia" in pathlib.Path("/proc/modules").read_text()
    ):
        nenv = _isolated_env(env, {"__EGL_VENDOR_LIBRARY_FILENAMES": nv_json})
        _cli(nenv, "config", "set", "verify-render", "false")
        _cli(nenv, "preview", "voronoi_gles3", "--seconds", "15")
        ran = _wait_until(lambda: _status(nenv).get("running"), 3.0)
        time.sleep(3.0)
        alive = _status(nenv).get("running")
        log_tail = ""
        with contextlib.suppress(OSError):
            log_tail = pathlib.Path(
                _rt_dir(), "ncz-screensaver", "hack.log"
            ).read_text()[-600:]
        _cli(nenv, "stop")
        _record(
            checks,
            "nvidia-egl-path",
            "pass",
            "informational: hack on the NVIDIA EGL vendor "
            + ("ran" if alive else "did not run")
            + (
                "; eglGetPlatformDisplay failed on the Wayland compositor"
                if "eglGetPlatformDisplay failed" in log_tail
                else ""
            ),
            metrics={"started": ran is not None, "alive_after_3s": bool(alive)},
        )
        _kill_hacks()


# ---------------------------------------------------------------------------
# Phase: idle (daemon, input dismissal, inhibit, lock chain, DPMS, unit).
# ---------------------------------------------------------------------------


def _start_idled(env, logpath):
    fh = open(logpath, "ab")  # noqa: SIM115 - owned by the child
    proc = subprocess.Popen(
        [IDLED, "--verbose"],
        env=env,
        stdout=fh,
        stderr=fh,
        stdin=subprocess.DEVNULL,
        start_new_session=True,
    )
    fh.close()
    return proc


def _stop_idled(proc):
    if proc and proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=3)


@_guard
def phase_idle(results, checks, env, workdir, launcher_path, idled_path, no_dpms, pw):
    if not os.path.exists(IDLED):
        _record(checks, "idled-installed", "fail", f"{IDLED} missing")
        return
    logs = os.path.join(workdir, "logs")
    os.makedirs(logs, exist_ok=True)
    was_new = _unit_active(env, IDLED_UNIT)
    was_old = _unit_active(env, OLD_IDLE_UNIT)
    _systemctl(env, "stop", IDLED_UNIT)
    _systemctl(env, "stop", OLD_IDLE_UNIT)
    subprocess.run(["pkill", "-x", "swayidle"], capture_output=True, check=False)
    marker = os.path.join(tempfile.mkdtemp(prefix="ncz-ht-lock-"), "locked")
    lock_script = os.path.join(os.path.dirname(marker), "lock.sh")
    pathlib.Path(lock_script).write_text(f"#!/bin/sh\ndate +%s > {marker}\n")
    os.chmod(lock_script, 0o755)
    ienv = _isolated_env(env, {"NCZ_LOCK_CMD": lock_script})
    proc = None
    try:
        for k, v in (
            ("mode", "one"),
            ("hack-id", "hyprsaver_aurora_gles3"),
            ("hack-idle-delay", "6"),
            ("lock-enabled", "false"),
            ("display-off-delay", "0"),
            ("lock-on-suspend", "false"),
        ):
            _cli(ienv, "config", "set", k, v)
        dry = subprocess.run(
            [IDLED, "--dry-run"], capture_output=True, text=True, env=ienv, check=False
        )
        _record(
            checks,
            "dry-run-plan",
            "pass"
            if '"saver":6' in dry.stdout and '"lock":null' in dry.stdout
            else "fail",
            dry.stdout.strip() or dry.stderr.strip(),
        )
        proc = _start_idled(ienv, os.path.join(logs, "idled.log"))
        up = _wait_until(lambda: _idled_json().get("state") == "active", 4.0)
        _record(
            checks,
            "daemon-start",
            "pass" if up is not None else "fail",
            f"state={_idled_json()}",
        )
        if up is None:
            return

        def saver_running():
            return _status(ienv).get("running")

        lat = _wait_until(saver_running, 6 + 6.0)
        _record(
            checks,
            "idle-fires-saver",
            "pass" if lat is not None else "fail",
            f"saver started {lat:.1f}s after daemon start (timer 6s)"
            if lat is not None
            else "saver never started",
            metrics={"seconds": lat},
        )
        if lat is not None:
            time.sleep(2.5)
            fa = _grab_frame(env)
            time.sleep(1.0)
            fb = _grab_frame(env)
            if fa and fb:
                cov = hti.coverage_fraction(fb[2])
                mot = hti.frame_diff_fraction(fa[2], fb[2])
                _record(
                    checks,
                    "idle-saver-visible",
                    "pass" if cov >= COVERAGE_GATE and mot >= MOTION_GATE else "fail",
                    f"coverage={cov:.3f} motion={mot:.3f}",
                    metrics={"coverage": cov, "motion": mot},
                )
            else:
                _record(checks, "idle-saver-visible", "fail", "screenshot failed")
            t0 = time.monotonic()
            pr = _poke(env, "motion")
            gone = _wait_until(
                lambda: not saver_running(), DISMISS_DEADLINE + 2, step=0.1
            )
            _record(
                checks,
                "dismiss-on-pointer",
                "pass" if gone is not None and pr.returncode == 0 else "fail",
                f"stopped {time.monotonic() - t0:.2f}s after injected motion (poke rc={pr.returncode} {pr.stderr[-120:]})",
                metrics={"seconds": gone},
            )
            back = _wait_until(lambda: _idled_json().get("state") == "active", 3.0)
            _record(
                checks,
                "state-returns-active",
                "pass" if back is not None else "fail",
                f"{_idled_json().get('state')}",
            )

            # Keyboard dismissal (virtual keyboard).
            again = _wait_until(saver_running, 6 + 6.0)
            if again is None:
                _record(
                    checks,
                    "dismiss-on-key",
                    "fail",
                    "saver did not restart for the key test",
                )
            else:
                time.sleep(1.5)
                pk = _poke(env, "key")
                gone = _wait_until(
                    lambda: not saver_running(), DISMISS_DEADLINE + 2, step=0.1
                )
                if pk.returncode != 0 and gone is None:
                    _record(
                        checks,
                        "dismiss-on-key",
                        "fail",
                        f"key injection failed rc={pk.returncode}: {pk.stderr[-150:]}",
                    )
                else:
                    _record(
                        checks,
                        "dismiss-on-key",
                        "pass" if gone is not None else "fail",
                        f"saver stopped {gone}s after key"
                        if gone is not None
                        else "saver still running",
                    )
            _poke(env, "motion")
            _wait_until(lambda: not saver_running(), 4.0)

        # Idle inhibitor: the saver must not start while a client inhibits idle.
        inh = subprocess.Popen(
            [
                sys.executable,
                os.environ.get("HT_WL_POKE")
                or os.path.join(
                    os.path.dirname(os.path.abspath(__file__)), "wl_poke.py"
                ),
                "inhibit",
                "--seconds",
                "24",
            ],
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        try:
            line = ""
            t0 = time.monotonic()
            while time.monotonic() - t0 < 8 and inh.poll() is None:
                line = inh.stdout.readline()
                if "inhibitor active" in line:
                    break
            if "inhibitor active" not in line:
                _record(
                    checks,
                    "idle-inhibit",
                    "fail",
                    f"inhibitor client did not start: {inh.stderr.read()[-200:] if inh.poll() is not None else line}",
                )
            else:
                _poke(env, "motion")  # start the idle clock from now
                early = _wait_until(saver_running, 6 + 8.0)
                _record(
                    checks,
                    "idle-inhibit-blocks",
                    "pass" if early is None else "fail",
                    "saver stayed off for 14 s while inhibited"
                    if early is None
                    else f"saver started after {early:.1f}s despite inhibitor",
                )
                inh.wait(timeout=30)
                after = _wait_until(saver_running, 6 + 6.0)
                _record(
                    checks,
                    "idle-inhibit-releases",
                    "pass" if after is not None else "fail",
                    f"saver started {after:.1f}s after the inhibitor ended"
                    if after is not None
                    else "saver never started after release",
                )
                _poke(env, "motion")
                _wait_until(lambda: not saver_running(), 4.0)
        finally:
            if inh.poll() is None:
                inh.kill()

        # Live reconfiguration.
        _cli(ienv, "config", "set", "hack-idle-delay", "9")
        ok = _wait_until(
            lambda: (_idled_json().get("plan") or {}).get("saver") == 9, 4.0
        )
        _record(
            checks,
            "live-reconfigure",
            "pass" if ok is not None else "fail",
            f"plan={_idled_json().get('plan')}",
        )

        # Lock chain: saver at 6, lock 4 s later, saver stopped before the lock command.
        _stop_idled(proc)
        _cli(ienv, "config", "set", "hack-idle-delay", "6")
        _cli(ienv, "config", "set", "lock-enabled", "true")
        _cli(ienv, "config", "set", "lock-delay", "4")
        pathlib.Path(marker).unlink(missing_ok=True)
        proc = _start_idled(ienv, os.path.join(logs, "idled-lock.log"))
        _wait_until(lambda: _idled_json().get("state") == "active", 4.0)
        started = _wait_until(saver_running, 12.0)
        locked = _wait_until(lambda: os.path.exists(marker), 10.0)
        stopped_first = not saver_running()
        _record(
            checks,
            "lock-chain",
            "pass"
            if started is not None and locked is not None and stopped_first
            else "fail",
            f"saver_start={started} lock_marker={locked is not None} saver_stopped_before_lock={stopped_first}",
        )
        _poke(env, "motion")
        _wait_until(lambda: not saver_running(), 4.0)

        # loginctl lock-session raises the logind Session.Lock signal.
        pathlib.Path(marker).unlink(missing_ok=True)
        ls = subprocess.run(
            ["loginctl", "lock-session"],
            capture_output=True,
            text=True,
            timeout=10,
            env=env,
            check=False,
        )
        got = _wait_until(lambda: os.path.exists(marker), 6.0)
        _record(
            checks,
            "logind-lock-signal",
            "pass" if got is not None else "fail",
            f"lock command ran {got:.1f}s after loginctl lock-session"
            if got is not None
            else f"no lock command after loginctl lock-session rc={ls.returncode} {ls.stderr[-100:]}",
        )
        # A lock client that dies must be replaced, never left as a black screen.
        _stop_idled(proc)
        flaky = os.path.join(os.path.dirname(marker), "flaky.sh")
        count = os.path.join(os.path.dirname(marker), "flaky.count")
        pathlib.Path(count).unlink(missing_ok=True)
        pathlib.Path(flaky).write_text(
            f"#!/bin/sh\necho x >> {count}\n[ $(wc -l < {count}) -ge 2 ] && exit 0\nexit 3\n"
        )
        os.chmod(flaky, 0o755)
        fenv = dict(ienv, NCZ_LOCK_CMD=flaky)
        proc = _start_idled(fenv, os.path.join(logs, "idled-flaky.log"))
        _wait_until(lambda: _idled_json().get("state") == "active", 4.0)
        subprocess.run(
            ["loginctl", "lock-session"],
            capture_output=True,
            timeout=10,
            env=env,
            check=False,
        )
        again = _wait_until(
            lambda: (
                os.path.exists(count)
                and len(pathlib.Path(count).read_text().split()) >= 2
            ),
            8.0,
        )
        _record(
            checks,
            "lock-client-respawn",
            "pass" if again is not None else "fail",
            f"a lock client that exited with status 3 was restarted after {again:.1f}s"
            if again is not None
            else "no replacement lock client was started after the first one failed",
        )
        _stop_idled(proc)
        _record(
            checks,
            "suspend-resume",
            "skip",
            "not run: no physical access to wake the host; the PrepareForSleep path shares the lock code exercised above",
        )

        # Real video playback (mpv fullscreen) must hold the saver off.
        if shutil.which("mpv"):
            _stop_idled(proc)
            _cli(ienv, "config", "set", "lock-enabled", "false")
            _cli(ienv, "set-mode", "one")
            _cli(ienv, "config", "set", "hack-idle-delay", "6")
            proc = _start_idled(ienv, os.path.join(logs, "idled-mpv.log"))
            _wait_until(lambda: _idled_json().get("state") == "active", 4.0)
            player = subprocess.Popen(
                [
                    "mpv",
                    "--no-config",
                    "--really-quiet",
                    "--fs",
                    "--no-audio",
                    "--loop=inf",
                    "--vo=gpu",
                    "--gpu-context=wayland",
                    "av://lavfi:testsrc2=size=1280x720:rate=30",
                ],
                env=env,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                start_new_session=True,
            )
            try:
                time.sleep(3.0)
                if player.poll() is not None:
                    _record(
                        checks,
                        "video-inhibit",
                        "skip",
                        f"mpv exited early rc={player.returncode}",
                    )
                else:
                    _poke(env, "motion")  # restart the idle clock; mpv keeps playing
                    early = _wait_until(saver_running, 6 + 8.0)
                    _record(
                        checks,
                        "video-inhibit-blocks",
                        "pass" if early is None else "fail",
                        "saver stayed off for 14 s while mpv played fullscreen video"
                        if early is None
                        else f"saver started {early:.1f}s into fullscreen video playback",
                    )
            finally:
                with contextlib.suppress(ProcessLookupError):
                    os.killpg(os.getpgid(player.pid), signal.SIGKILL)
            after = _wait_until(saver_running, 6 + 6.0)
            _record(
                checks,
                "video-inhibit-releases",
                "pass" if after is not None else "fail",
                f"saver started {after:.1f}s after the player was closed"
                if after is not None
                else "saver never started after the player closed",
            )
            _poke(env, "motion")
            _wait_until(lambda: not saver_running(), 4.0)
        else:
            _record(checks, "video-inhibit", "skip", "mpv not installed")

        # DPMS.
        if no_dpms:
            _record(checks, "dpms", "skip", "--no-dpms")
        else:
            _stop_idled(proc)
            _cli(ienv, "config", "set", "lock-enabled", "false")
            _cli(ienv, "set-mode", "off")
            _cli(ienv, "config", "set", "display-off-delay", "12")
            proc = _start_idled(ienv, os.path.join(logs, "idled-dpms.log"))
            _wait_until(lambda: _idled_json().get("state") == "active", 4.0)
            off = _wait_until(lambda: _idled_json().get("state") == "display-off", 18.0)
            time.sleep(1.0)
            frame = _grab_frame(env)
            dark = frame is None or hti.coverage_fraction(frame[2]) < 0.005
            _poke(env, "motion")
            back = _wait_until(lambda: _idled_json().get("state") == "active", 5.0)
            time.sleep(1.0)
            frame2 = _grab_frame(env)
            lit = frame2 is not None and hti.coverage_fraction(frame2[2]) > 0.02
            _record(
                checks,
                "dpms-off-on",
                "pass" if off is not None and back is not None and lit else "fail",
                f"display-off state={off is not None}, capture dark while off={dark}, back to active={back is not None}, lit after input={lit}",
            )
    finally:
        _stop_idled(proc)
        _cli(env, "stop")
        _kill_hacks()
        outs = _read_proc("wlr-randr", [])
        _record(
            checks,
            "outputs-enabled-after-tests",
            "pass" if outs and "Enabled: no" not in outs else "fail",
            "all outputs enabled"
            if "Enabled: no" not in outs
            else "an output is left disabled",
        )

    # The real user unit.
    _systemctl(env, "daemon-reload")
    _systemctl(env, "start", IDLED_UNIT)
    act = _wait_until(lambda: _unit_active(env, IDLED_UNIT), 4.0)
    j = subprocess.run(
        ["journalctl", "--user", "-u", IDLED_UNIT, "-n", "20", "--no-pager"],
        capture_output=True,
        text=True,
        env=env,
        check=False,
    )
    pathlib.Path(logs, "idled-unit-journal.txt").write_text(j.stdout)
    _record(
        checks,
        "systemd-unit",
        "pass" if act is not None else "fail",
        "unit active"
        if act is not None
        else "unit did not stay active (see logs/idled-unit-journal.txt)",
        evidence="logs/idled-unit-journal.txt",
    )
    enabled = _systemctl(env, "is-enabled", IDLED_UNIT).stdout.strip()
    wants = os.path.exists(
        "/etc/systemd/user/graphical-session.target.wants/ncz-screensaver-idled.service"
    )
    _record(
        checks,
        "unit-enabled-for-login",
        "pass" if enabled == "enabled" and wants else "fail",
        f"is-enabled={enabled}, graphical-session.target.wants link={wants}",
    )
    # A restart of the unit (what a re-login does) must come back with a plan.
    _systemctl(env, "restart", IDLED_UNIT)
    back = _wait_until(lambda: bool(_idled_json().get("plan")), 5.0)
    _record(
        checks,
        "unit-restart",
        "pass" if back is not None else "fail",
        f"state file after restart: {_idled_json()}",
    )
    _systemctl(env, "stop", IDLED_UNIT)
    # Restore what was running before the phase.
    if was_new:
        _systemctl(env, "start", IDLED_UNIT)
    elif was_old:
        _systemctl(env, "start", OLD_IDLE_UNIT)


# ---------------------------------------------------------------------------
# Phase: color (black hole modes).
# ---------------------------------------------------------------------------


def _graphical_session():
    """(session id, labwc pid) of the seat0 graphical session, or (None, 0)."""
    out = subprocess.run(
        ["loginctl", "list-sessions", "--no-legend"],
        capture_output=True,
        text=True,
        check=False,
    ).stdout
    sid = None
    for line in out.splitlines():
        f = line.split()
        if len(f) >= 4 and f[3] == "seat0":
            sid = f[0]
    return sid, _compositor_pid()[1]


def _greetd_login(user, pw):
    """Log `user` in through the greetd socket (as root). True on success."""
    script = os.path.join(os.path.dirname(os.path.abspath(__file__)), "greetd_login.py")
    r = _run_with_sudo(
        [sys.executable, script, user], pw=pw, timeout=60.0, stdin_text=pw + "\n"
    )
    return r.returncode == 0


@_guard
def phase_session(results, checks, env, workdir, pw):
    """Scripted logout and login: `systemctl restart greetd` ends the session
    and shows the greeter; greetd_login.py then logs the user in again through
    the greetd IPC socket. Afterwards the idle daemon must have started by
    itself and a hack must render."""
    user = os.environ.get("USER") or pathlib.Path("/proc/self").owner()
    if not pw or not glob.glob("/run/greetd*.sock"):
        _record(
            checks,
            "session-restart",
            "skip",
            "greetd socket or sudo secret unavailable",
        )
        return
    old_sid, old_pid = _graphical_session()
    was_active = _unit_active(env, IDLED_UNIT)
    new_ok = False
    try:
        r = _run_with_sudo(["systemctl", "restart", "greetd"], pw=pw, timeout=60.0)
        if r.returncode != 0:
            _record(
                checks,
                "session-restart",
                "fail",
                f"greetd restart failed: {r.stderr[-200:]}",
            )
            return
        greeter = _wait_until(
            lambda: (
                bool(glob.glob("/run/greetd*.sock"))
                and bool(_pids("singularity-greeter"))
            ),
            40.0,
            step=1.0,
        )
        time.sleep(3.0)
        logged = greeter is not None and _greetd_login(user, pw)

        def new_session():
            _sid, pid = _graphical_session()
            return (
                bool(pid)
                and pid != old_pid
                and pathlib.Path(_rt_dir(), "wayland-0").exists()
            )

        got = _wait_until(new_session, 90.0, step=1.0) if logged else None
        new_ok = got is not None
        sid, pid = _graphical_session()
        _record(
            checks,
            "session-restart",
            "pass" if new_ok else "fail",
            f"logout via greetd restart, scripted login: seat0 session {old_sid} -> {sid}, "
            f"compositor pid {old_pid} -> {pid} after {got or 0:.0f}s"
            if new_ok
            else f"no new graphical session (greeter up: {greeter is not None}, login ok: {logged})",
        )
        if not new_ok:
            return
        time.sleep(8.0)  # let the shell finish starting
        up = _wait_until(lambda: _unit_active(env, IDLED_UNIT), 30.0, step=1.0)
        _record(
            checks,
            "unit-autostart-after-login",
            "pass" if up is not None else "fail",
            f"{IDLED_UNIT} active {up:.0f}s after login (was active before: {was_active})"
            if up is not None
            else f"{IDLED_UNIT} did not start with the new session",
        )
        ok = _display_probe(_build_session_env())
        _record(
            checks,
            "saver-after-login",
            "pass" if ok else "fail",
            "a hack renders in the new session"
            if ok
            else "no hack visible after re-login",
        )
    finally:
        if (
            not new_ok
            and glob.glob("/run/greetd*.sock")
            and not _graphical_session()[1]
        ):
            _greetd_login(user, pw)  # never leave the host at the greeter


@_guard
def phase_multioutput(results, checks, env, workdir, pw):
    """Two virtual outputs: run a private headless labwc next to the real
    session and point the idle daemon and launcher at it."""
    labwc = "/opt/singularity/bin/labwc"
    if not os.path.exists(labwc):
        _record(
            checks,
            "multioutput",
            "skip",
            "no labwc binary to start a headless compositor",
        )
        return
    rt = _rt_dir()
    before = set(glob.glob(os.path.join(rt, "wayland-*")))
    cenv = {
        "PATH": os.environ.get("PATH", "/usr/bin:/bin"),
        "HOME": os.environ.get("HOME", "/tmp"),
        "XDG_RUNTIME_DIR": rt,
        "WLR_BACKENDS": "headless",
        "WLR_HEADLESS_OUTPUTS": "2",
        "WLR_LIBINPUT_NO_DEVICES": "1",
    }
    logs = os.path.join(workdir, "logs")
    os.makedirs(logs, exist_ok=True)
    with open(os.path.join(logs, "headless-labwc.log"), "wb") as lf:
        comp = subprocess.Popen(
            [labwc],
            env=cenv,
            stdout=lf,
            stderr=lf,
            stdin=subprocess.DEVNULL,
            start_new_session=True,
        )
    proc = None
    try:

        def new_socket():
            new = [
                p
                for p in glob.glob(os.path.join(rt, "wayland-*"))
                if not p.endswith(".lock") and p not in before
            ]
            return os.path.basename(new[0]) if new else None

        _wait_until(lambda: new_socket() is not None, 10.0)
        sock = new_socket()
        if not sock:
            _record(
                checks,
                "headless-compositor",
                "skip",
                "the headless labwc did not start (see logs/headless-labwc.log)",
            )
            return
        nenv = _isolated_env(env, {"WAYLAND_DISPLAY": sock})
        randr = subprocess.run(
            ["wlr-randr"],
            capture_output=True,
            text=True,
            env=nenv,
            timeout=10,
            check=False,
        ).stdout
        names = [
            ln.split()[0] for ln in randr.splitlines() if ln and not ln.startswith(" ")
        ]
        _record(
            checks,
            "headless-outputs",
            "pass" if len(names) >= 2 else "fail",
            f"{sock} advertises outputs {names}",
        )
        if len(names) < 2:
            return
        for k, v in (
            ("mode", "off"),
            ("lock-enabled", "false"),
            ("display-off-delay", "6"),
            ("hack-idle-delay", "300"),
        ):
            _cli(nenv, "config", "set", k, v)
        idlog = os.path.join(logs, "idled-headless.log")
        proc = _start_idled(nenv, idlog)
        _wait_until(
            lambda: _idled_json().get("state") in ("active", "display-off"), 5.0
        )
        text = lambda: pathlib.Path(idlog).read_text(errors="replace")
        bound = text().count("bound output power")
        _record(
            checks,
            "idled-binds-every-output",
            "pass" if bound >= 2 else "fail",
            f"idled created output-power objects for {bound} outputs",
        )
        off = _wait_until(
            lambda: text().count("output power mode event: mode=0") >= 2, 14.0
        )
        _record(
            checks,
            "dpms-all-outputs-off",
            "pass" if off is not None else "fail",
            "both outputs acknowledged power off"
            if off is not None
            else f"power-off acknowledged by {text().count('output power mode event: mode=0')} of {bound} outputs",
        )
        on_before = text().count("output power mode event: mode=1")
        _poke(nenv, "motion")
        on = _wait_until(
            lambda: (
                text().count("output power mode event: mode=1") >= on_before + 2
                and _idled_json().get("state") == "active"
            ),
            6.0,
        )
        _record(
            checks,
            "dpms-all-outputs-on",
            "pass" if on is not None else "fail",
            f"state after input: {_idled_json().get('state')}",
        )
        _stop_idled(proc)
        proc = None
        # Where does a hack appear? Informational: current hacks bind one output.
        _cli(nenv, "config", "set", "verify-render", "false")
        _cli(nenv, "preview", "voronoi_gles3", "--seconds", "20")
        running = _wait_until(lambda: _status(nenv).get("running"), 6.0)
        time.sleep(3.0)
        cov = {}
        for name in names[:2]:
            fd, path = tempfile.mkstemp(suffix=".ppm")
            os.close(fd)
            p = subprocess.run(
                ["grim", "-o", name, "-s", "0.25", "-t", "ppm", path],
                capture_output=True,
                env=nenv,
                timeout=15,
                check=False,
            )
            if p.returncode == 0:
                with contextlib.suppress(OSError, hti.PPMError):
                    cov[name] = round(
                        hti.coverage_fraction(
                            hti.parse_ppm(pathlib.Path(path).read_bytes())[2]
                        ),
                        3,
                    )
            os.unlink(path)
        _cli(nenv, "stop")
        covered = [n for n, c in cov.items() if c > 0.5]
        _record(
            checks,
            "hack-on-outputs",
            "pass" if running is not None and covered else "fail",
            f"informational: hack covers {covered or 'no output'} of {names[:2]}; coverage per output {cov} "
            "(hacks bind a single output; per-output instances are an open item)",
            metrics={"coverage": cov},
        )
    finally:
        _stop_idled(proc)
        with contextlib.suppress(ProcessLookupError):
            os.killpg(os.getpgid(comp.pid), signal.SIGTERM)
        with contextlib.suppress(subprocess.TimeoutExpired):
            comp.wait(timeout=5)
        _cli(env, "stop")


@_guard
def phase_color(results, checks, env, workdir, launcher_path):
    shots = os.path.join(workdir, "shots")
    os.makedirs(shots, exist_ok=True)
    # A fixed seed makes the scene identical between runs so only the color
    # model can explain a difference between the modes.
    ienv = _isolated_env(env, {"NCZ_BLACKHOLE_FIXED_SEED": "12345"})
    _cli(ienv, "config", "set", "verify-render", "false")
    stats = {}
    for mode in ("stylized", "kipthorne", "faithful"):
        _cli(ienv, "stop")
        _wait_until(lambda: not _status(ienv).get("running"), 4.0)
        _cli(ienv, "set-color", mode)
        _cli(ienv, "preview", "blackhole_gles3", "--seconds", "30")
        _wait_until(lambda: _status(ienv).get("running"), 5.0)
        time.sleep(5.0)
        fr = _grab_frame(env, 0.125)
        png = os.path.join(shots, f"blackhole_{mode}.png")
        _screenshot_to_png("", png, env, 0.25)
        if not fr:
            _record(checks, f"color-{mode}", "fail", "screenshot failed")
            continue
        cov = hti.coverage_fraction(fr[2])
        r, g, b, _n = hti.mean_brightness_of_bright_pixels(fr[2])
        stats[mode] = (r, g, b)
        _record(
            checks,
            f"color-{mode}",
            "pass" if cov >= COLOR_COVERAGE_GATE else "fail",
            f"coverage={cov:.3f} mean_rgb=({r:.0f},{g:.0f},{b:.0f})",
            evidence=f"shots/blackhole_{mode}.png",
            metrics={"coverage": cov, "r": r, "g": g, "b": b},
        )
    _cli(ienv, "stop")
    if len(stats) == 3:

        def dist(m1, m2):
            a, b = stats[m1], stats[m2]
            d = hti.chromaticity_distance(a, b)
            ratio = hti.brightness_ratio(sum(a), sum(b))
            # 1.0 or more means visibly different by the harness thresholds.
            score = max(
                d / COLOR_CHROMA_MIN, abs(ratio - 1.0) / (COLOR_BRIGHTNESS_MIN - 1.0)
            )
            return score, d, ratio

        for label, pairs in (
            (
                "color-stylized-distinct",
                (("stylized", "kipthorne"), ("stylized", "faithful")),
            ),
            ("color-physical-modes-distinct", (("kipthorne", "faithful"),)),
        ):
            got = [(p, *dist(*p)) for p in pairs]
            ok = all(g[1] >= 1.0 for g in got)
            _record(
                checks,
                label,
                "pass" if ok else "fail",
                "; ".join(
                    f"{p[0]} vs {p[1]}: score {sc:.2f} (chroma {d:.3f}, brightness x{r:.2f})"
                    for p, sc, d, r in got
                ),
                metrics={f"{p[0]}-{p[1]}": sc for p, sc, _d, _r in got},
            )
    # Unknown value: must warn and fall back, not crash.
    _cli(ienv, "config", "set", "blackhole-color-mode", "bogus")
    _cli(ienv, "preview", "blackhole_gles3", "--seconds", "12")
    alive = _wait_until(lambda: _status(ienv).get("running"), 6.0)
    time.sleep(3.0)
    still = _status(ienv).get("running")
    _record(
        checks,
        "color-unknown-value",
        "pass" if alive is not None and still else "fail",
        "hack kept running with an unknown color value"
        if still
        else "hack died with an unknown color value",
    )
    _cli(ienv, "stop")


# ---------------------------------------------------------------------------
# Phase: chooser (settings app).
# ---------------------------------------------------------------------------


@_guard
def phase_chooser(results, checks, env, workdir, settings_path, launcher_path):
    shots = os.path.join(workdir, "shots")
    os.makedirs(shots, exist_ok=True)
    ienv = _isolated_env(env)
    if not os.path.exists(SETTINGS_APP):
        _record(checks, "settings-installed", "fail", f"{SETTINGS_APP} missing")
        return
    d = subprocess.run(
        [SETTINGS_APP, "--dump"],
        capture_output=True,
        text=True,
        timeout=30,
        env=ienv,
        check=False,
    )
    try:
        dump = json.loads(d.stdout)
        n = len(dump.get("catalog", []))
    except ValueError:
        n = -1
    _record(
        checks,
        "settings-dump",
        "pass" if n >= 80 else "fail",
        f"{n} catalog entries in --dump {d.stderr[-120:]}",
    )
    try:
        s = subprocess.run(
            [SETTINGS_APP, "--self-test"],
            capture_output=True,
            text=True,
            timeout=60,
            env=ienv,
            check=False,
        )
        lines = [ln for ln in s.stdout.splitlines() if ln.startswith(("PASS", "FAIL"))]
        bad = [ln for ln in lines if ln.startswith("FAIL")]
        pathlib.Path(workdir, "logs", "settings-selftest.txt").write_text(
            s.stdout + s.stderr
        )
        _record(
            checks,
            "settings-self-test",
            "pass" if s.returncode == 0 and lines and not bad else "fail",
            f"rc={s.returncode}, {len(lines)} checks, failures={bad[:3]} {s.stderr[-150:]}",
            evidence="logs/settings-selftest.txt",
        )
    except subprocess.TimeoutExpired:
        _record(checks, "settings-self-test", "fail", "timed out after 60 s")
    app = subprocess.Popen(
        [SETTINGS_APP],
        env=ienv,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        start_new_session=True,
    )
    try:
        time.sleep(5.0)
        png = os.path.join(shots, "settings.png")
        ok, detail = _screenshot_to_png("", png, env, 0.5)
        _record(
            checks,
            "settings-screenshot",
            "pass" if ok and app.poll() is None else "fail",
            f"window alive={app.poll() is None}; {detail}",
            evidence="shots/settings.png",
        )
    finally:
        app.terminate()
        with contextlib.suppress(subprocess.TimeoutExpired):
            app.wait(timeout=5)


def _summarize(checks_by_phase: dict[str, list[Check]]) -> dict[str, int]:
    out = {"pass": 0, "fail": 0, "skip": 0}
    for phase_checks in checks_by_phase.values():
        for c in phase_checks:
            out[c.status] = out.get(c.status, 0) + 1
    return out


def _write_results(
    path: str,
    results: dict[str, Any],
    checks_by_phase: dict[str, list[Check]],
) -> None:
    """Atomically write ``results.json``.

    The format merges the free-form fields the caller populated into
    ``results`` with a ``phases`` mapping of ``phase -> list[Check]`` and a
    ``summary`` counters block.
    """
    phases_dict: dict[str, list[dict[str, Any]]] = {}
    for phase, checks in checks_by_phase.items():
        phases_dict[phase] = [c.to_dict() for c in checks]
    results["phases"] = phases_dict
    results["summary"] = _summarize(checks_by_phase)
    results["finished"] = _now_iso()
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as fh:
        json.dump(results, fh, indent=2, sort_keys=True)
        fh.write("\n")
    os.replace(tmp, path)


# ---------------------------------------------------------------------------
# Phase orchestration.
# ---------------------------------------------------------------------------


def run_phases(
    phases: list[str],
    *,
    deb: str | None,
    hack_ids: list[str],
    seconds: float,
    no_dpms: bool,
    workdir: str,
    results: dict[str, Any],
    env: dict[str, str],
    pw: str | None,
    catalog_override: str | None,
) -> int:
    """Run the selected phases; return 0 only if every check passes."""
    checks_by_phase: dict[str, list[Check]] = {p: [] for p in phases}

    # Locate the catalog and the binaries the phases need.
    catalog_env = catalog_override or os.environ.get("HT_CATALOG", CATALOG_PATH_DEFAULT)
    os.environ["HT_CATALOG"] = catalog_env
    launcher_path = "/usr/bin/ncz-screensaver"
    idled_path = "/usr/libexec/ncz-screensaver-idled"
    settings_path = "/usr/bin/ncz-screensaver-settings"

    # Re-establish the XDG vars so subprocesses inherit the session env.
    os.environ.setdefault("WAYLAND_DISPLAY", "wayland-0")
    runtime_dir = os.environ.get("XDG_RUNTIME_DIR") or f"/run/user/{os.getuid()}"
    os.environ["XDG_RUNTIME_DIR"] = runtime_dir
    dbus_addr = os.environ.get(
        "DBUS_SESSION_BUS_ADDRESS", f"unix:path={runtime_dir}/bus"
    )
    os.environ["DBUS_SESSION_BUS_ADDRESS"] = dbus_addr

    _log(f"selected phases: {phases}")
    _log(f"hack_ids: {hack_ids}")

    overall_fail = False
    # The older swayidle based manager would lock the session in the middle of
    # a long run; pause it and restore it at the end.
    paused = []
    for unit in (OLD_IDLE_UNIT, IDLED_UNIT):
        if _unit_active(env, unit):
            paused.append(unit)
            _systemctl(env, "stop", unit)
    subprocess.run(["pkill", "-x", "swayidle"], capture_output=True, check=False)
    if "env" in phases:
        results["_pw"] = pw
        phase_env(results, checks_by_phase["env"], env)
        results.pop("_pw", None)
    if "install" in phases:
        phase_install(results, checks_by_phase["install"], env, deb, pw, workdir)
    if "hacks" in phases:
        results["_pw"] = pw
        # Resolve now: the catalog on the host is only current once the package
        # under test is installed (an older catalog may exist beforehand).
        hack_ids = _resolve_hacks(os.environ.get("HT_HACKS_ARG", "all"))
        _log(f"hack_ids resolved after install: {len(hack_ids)}")
    if "hacks" in phases:
        phase_hacks(
            results,
            checks_by_phase["hacks"],
            env,
            workdir,
            hack_ids,
            seconds,
            launcher_path,
        )
    results.pop("_pw", None)
    if "launcher" in phases:
        phase_launcher(
            results,
            checks_by_phase["launcher"],
            env,
            workdir,
            launcher_path,
        )
    if "idle" in phases:
        phase_idle(
            results,
            checks_by_phase["idle"],
            env,
            workdir,
            launcher_path,
            idled_path,
            no_dpms,
            pw,
        )
    if "multioutput" in phases:
        phase_multioutput(results, checks_by_phase["multioutput"], env, workdir, pw)
    if "session" in phases:
        results["_pw"] = pw
        phase_session(results, checks_by_phase["session"], env, workdir, pw)
        results.pop("_pw", None)
    if "color" in phases:
        phase_color(
            results,
            checks_by_phase["color"],
            env,
            workdir,
            launcher_path,
        )
    if "chooser" in phases:
        phase_chooser(
            results,
            checks_by_phase["chooser"],
            env,
            workdir,
            settings_path,
            launcher_path,
        )

    for unit in paused:
        _systemctl(env, "start", unit)

    # Write results.json after every phase ran. We always write so the
    # controller can rescue partial output if a later phase died hard.
    _write_results(os.path.join(workdir, "results.json"), results, checks_by_phase)

    for phase, checks in checks_by_phase.items():
        for c in checks:
            if c.status == "fail":
                overall_fail = True
                _log(f"FAIL {phase}/{c.name}: {c.detail}")
    return 1 if overall_fail else 0


# ---------------------------------------------------------------------------
# CLI.
# ---------------------------------------------------------------------------


def _populate_host_facts(results: dict[str, Any]) -> None:
    """Stamp a few machine-derived facts into ``results`` at start time."""
    results["started"] = _now_iso()
    results["host"] = socket.gethostname()
    results["arch"] = (
        _read_proc("dpkg", ["--print-architecture"]).strip() or os.uname().machine
    )
    results["kernel"] = _read_proc("uname", ["-r"]).strip() or os.uname().release
    results["os_release"] = _read_os_release()


def _selftest() -> int:
    """Exercise the pure PPM helpers on synthetic frames and report PASS/FAIL."""
    failures = 0

    def case(name: str, ok: bool, detail: str) -> None:
        nonlocal failures
        if ok:
            print(f"PASS {name}: {detail}")
        else:
            print(f"FAIL {name}: {detail}")
            failures += 1

    # Build a 32x18 PPM (a downscaled 480x270 / 15) with 3 horizontal bands.
    width, height = 32, 18
    pixels = []
    for y in range(height):
        for x in range(width):
            if y < height // 3:
                pixels.append((255, 0, 0))  # red band
            elif y < 2 * height // 3:
                pixels.append((0, 255, 0))  # green band
            else:
                pixels.append((0, 0, 255))  # blue band
    out = bytearray()
    out.extend(f"P6\n{width} {height}\n255\n".encode("ascii"))
    for r, g, b in pixels:
        out.extend((r, g, b))
    data = bytes(out)
    w, h, rgb = hti.parse_ppm(data)
    case("parse_ppm-dims", w == width and h == height, f"{w}x{h}")
    case("parse_ppm-len", len(rgb) == width * height * 3, f"{len(rgb)} bytes")

    cov = hti.coverage_fraction(rgb)
    case("coverage-uniform", abs(cov - 1.0) < 1e-9, f"coverage={cov:.3f}")

    # Black frame -> coverage == 0
    black = b"\x00\x00\x00" * (width * height)
    case("coverage-black", hti.coverage_fraction(black) == 0.0, "all-zero")

    # Frame diff between two identical frames == 0
    diff_self = hti.frame_diff_fraction(rgb, rgb)
    case("diff-same", diff_self == 0.0, f"diff={diff_self:.4f}")

    # Frame diff between completely different frames == 1
    white = b"\xff\xff\xff" * (width * height)
    diff_white = hti.frame_diff_fraction(rgb, white)
    case(
        "diff-black-white",
        diff_white > 0.9,
        f"diff={diff_white:.3f}",
    )

    # Mean brightness of the synthetic frame: every pixel passes threshold,
    # mean R/G/B are each (height_band_fraction) of 255.
    r, g, b, n = hti.mean_brightness_of_bright_pixels(rgb)
    case("mean-counts", n == width * height, f"n={n}")
    case(
        "mean-channels",
        abs(r - 85.0) < 10 and abs(g - 85.0) < 10 and abs(b - 85.0) < 10,
        f"({r:.1f},{g:.1f},{b:.1f})",
    )

    # Chromaticity of red vs blue should be large.
    d_rb = hti.chromaticity_distance((255, 0, 0), (0, 0, 255))
    case("chroma-rb", abs(d_rb - 1.0) < 1e-6, f"dist={d_rb:.3f}")

    # Performance: 480x270 PPM should parse + compute coverage in well under
    # one second; give it 5x headroom for slow CI runners.
    import random

    rng = random.Random(0)
    big = bytes(rng.randrange(256) for _ in range(480 * 270 * 3))
    big_ppm = b"P6\n480 270\n255\n" + big
    t0 = time.monotonic()
    _, _, buf = hti.parse_ppm(big_ppm)
    cov_big = hti.coverage_fraction(buf)
    elapsed = time.monotonic() - t0
    case(
        "perf-parse-coverage",
        elapsed < 1.0,
        f"480x270 in {elapsed:.3f}s (cov={cov_big:.3f})",
    )

    print(f"PASS total: {6 + 1 + 1 + 1 + 1 + 1 + 1 - failures}/{7}")
    return 0 if failures == 0 else 1


def _build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="host-test-agent",
        description="Run the NCZ screensaver test phases inside a Wayland session.",
    )
    p.add_argument(
        "--phases",
        default="env,install,hacks,launcher,idle,color,chooser",
        help="Comma-separated list of phases (default: all).",
    )
    p.add_argument(
        "--deb",
        default=None,
        help="Optional path to a .deb package to install in the install phase.",
    )
    p.add_argument(
        "--hacks",
        default="all",
        help="Comma-separated hack ids, 'all', or 'smoke' (default: all).",
    )
    p.add_argument(
        "--seconds",
        type=float,
        default=8.0,
        help="Seconds per hack in the hacks phase (default: 8).",
    )
    p.add_argument(
        "--no-dpms",
        action="store_true",
        help="Skip the DPMS sub-test in the idle phase.",
    )
    p.add_argument(
        "--workdir",
        default=".",
        help="Result directory (default: cwd).",
    )
    p.add_argument(
        "--catalog",
        default=None,
        help="Path to the catalog TSV (default: /usr/share/ncz-screensavers/hacks.tsv).",
    )
    p.add_argument(
        "--selftest",
        action="store_true",
        help="Exercise the PPM helpers on synthetic frames and exit.",
    )
    return p


def _resolve_hacks(arg: str) -> list[str]:
    catalog_paths = [
        os.environ.get("HT_CATALOG", CATALOG_PATH_DEFAULT),
        CATALOG_FALLBACK,
    ]
    catalog = _read_catalog(catalog_paths)
    if arg == "all":
        return sorted(catalog.keys())
    if arg == "smoke":
        return [
            "blackhole_gles3",
            "hyprsaver_aurora_gles3",
            "xshadertoy_alienbeacon_gles3",
            "voronoi_gles3",
        ]
    return [h.strip() for h in arg.split(",") if h.strip()]


def main(argv: list[str] | None = None) -> int:
    args = _build_parser().parse_args(argv)
    if args.selftest:
        return _selftest()

    pw = _read_sudo_secret()
    workdir = os.path.abspath(args.workdir)
    os.makedirs(workdir, exist_ok=True)
    os.makedirs(os.path.join(workdir, "shots"), exist_ok=True)
    os.makedirs(os.path.join(workdir, "logs"), exist_ok=True)
    os.environ["HT_AGENT_LOG"] = os.path.join(workdir, "agent.log")
    _log(f"agent start workdir={workdir}")

    env = _build_session_env()
    results: dict[str, Any] = {"outputs": [], "phases": {}}
    _populate_host_facts(results)

    phases = [p.strip() for p in args.phases.split(",") if p.strip()]
    os.environ["HT_HACKS_ARG"] = args.hacks
    hack_ids = _resolve_hacks(args.hacks)
    return run_phases(
        phases,
        deb=args.deb,
        hack_ids=hack_ids,
        seconds=args.seconds,
        no_dpms=args.no_dpms,
        workdir=workdir,
        results=results,
        env=env,
        pw=pw,
        catalog_override=args.catalog,
    )


if __name__ == "__main__":
    raise SystemExit(main())
