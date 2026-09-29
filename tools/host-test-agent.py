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
        input=stdin_text if pw is not None else None,
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
    """Find the kernel driver currently bound to the display class."""
    raw = _read_proc("lspci", ["-nnk", "-d", "::0300"])
    for line in raw.splitlines():
        line = line.strip()
        if line.startswith("Kernel driver in use:"):
            return line.split(":", 1)[1].strip()
    return ""


def _egl_info() -> dict[str, str]:
    """Best-effort EGL renderer string; never fails the phase."""
    out: dict[str, str] = {}
    # ``eglinfo -B`` prints a short block including "EGL version" and
    # "EGL client APIs" on Mesa. ``es2_info`` and ``es_info`` are similar.
    for cmd in (["eglinfo", "-B"], ["es2_info"], ["es_info"], ["glxinfo"]):
        try:
            proc = _run(cmd, timeout=5.0)
        except (FileNotFoundError, OSError, subprocess.TimeoutExpired):
            continue
        if proc.returncode != 0:
            continue
        text = proc.stdout.decode("utf-8", errors="replace")
        for line in text.splitlines():
            low = line.lower()
            if (
                "renderer" in low
                or "vendor" in low
                or "version" in low
                or "client apis" in low
            ):
                out[line.strip()] = ""
        if out:
            out["__source"] = cmd[0]
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
    script = os.environ.get("HT_WL_POKE", "")
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


def phase_env(
    results: dict[str, Any], checks: list[Check], env: dict[str, str]
) -> None:
    """Record host facts into ``results`` and the env-phase ``checks``."""
    gpu: dict[str, Any] = {}

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
        _record(checks, "driver", "pass", f"{driver} bound to display class")
    else:
        _record(checks, "driver", "fail", "no kernel driver bound to display class")

    egl = _egl_info()
    gpu["egl"] = egl
    if egl:
        _record(checks, "egl-info", "pass", f"source={egl.get('__source', '?')}")
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
            _record(checks, f"service-{svc}", "fail", f"already running: {state}")
        elif state in ("inactive", "failed", ""):
            _record(checks, f"service-{svc}", "pass", state or "absent")
        else:
            _record(checks, f"service-{svc}", "skip", f"unexpected state: {state}")

    swayidle = _read_proc("pgrep", ["-x", "swayidle"])
    if swayidle.strip():
        _record(checks, "swayidle", "fail", "swayidle running; not allowed")
    else:
        _record(checks, "swayidle", "pass", "no swayidle process")


# ---------------------------------------------------------------------------
# Phase: install.
# ---------------------------------------------------------------------------


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
            ["apt-get", "install", "-s", f"./{remote_deb}"],
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
                "DEBIAN_FRONTEND=noninteractive",
                "apt-get",
                "install",
                "-y",
                f"./{remote_deb}",
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
            proc.stderr.decode("utf-8", errors="replace")[:400],
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
        _, _, b_buf = hti.parse_ppm(pathlib.Path(ppm_b_path).read_bytes())
        _, _, base_buf = hti.parse_ppm(pathlib.Path(baseline_path).read_bytes())
    except (OSError, hti.PPMError) as exc:
        out["error"] = str(exc)
        return out
    out["coverage"] = hti.coverage_fraction(b_buf)
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

        sub: dict[str, Any] = {
            "process_alive": alive,
            "leftover": leftover,
            "started_via": started_via,
            "frame_a_ok": ok_a,
            "frame_b_ok": ok_b,
            "png_ok": png_ok,
        }
        sub.update(metrics)

        passed = (
            ok_b
            and png_ok
            and alive
            and not leftover
            and metrics.get("coverage", 0.0) >= COVERAGE_GATE
            and metrics.get("motion", 0.0) >= MOTION_GATE
            and metrics.get("baseline_diff", 0.0) >= BASELINE_DIFF_GATE
        )
        status = "pass" if passed else "fail"
        detail = (
            f"coverage={metrics.get('coverage', 0):.3f} "
            f"motion={metrics.get('motion', 0):.4f} "
            f"baseline_diff={metrics.get('baseline_diff', 0):.3f} "
            f"alive={alive} leftover={leftover}"
        )
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
# Phase: launcher.
# ---------------------------------------------------------------------------


def _isolated_config_env(extra: dict[str, str] | None = None) -> dict[str, str]:
    """Build an isolated env with GSettings keyfile backend.

    The launcher reads ``$XDG_CONFIG_HOME/dev.ncz/screensaver/`` via the
    keyfile backend; this lets the harness tweak values without touching
    dconf. The schema is loaded from the system-wide compiled location.
    """
    cfg_dir = tempfile.mkdtemp(prefix="ncz-host-test-cfg-")
    os.makedirs(os.path.join(cfg_dir, "dev.ncz", "screensaver"), exist_ok=True)
    env = dict(os.environ)
    env["XDG_CONFIG_HOME"] = cfg_dir
    env["GSETTINGS_BACKEND"] = "keyfile"
    env["GSETTINGS_SCHEMA_DIR"] = SYSTEM_SCHEMA_DIR
    if extra:
        env.update(extra)
    return env


def _launcher_cmd(
    launcher: str,
    cmd_args: list[str],
    env: dict[str, str],
    timeout: float = 10.0,
) -> subprocess.CompletedProcess:
    return _run([launcher, *cmd_args], timeout=timeout, env=env)


def phase_launcher(
    results: dict[str, Any],
    checks: list[Check],
    env: dict[str, str],
    workdir: str,
    launcher_path: str,
) -> None:
    if not os.path.exists(launcher_path):
        _record(checks, "launcher-cli", "fail", f"missing: {launcher_path}")
        return
    ienv = _isolated_config_env()
    # Ensure a clean slate.
    _launcher_cmd(launcher_path, ["stop"], ienv, timeout=5.0)

    # list --json sanity.
    try:
        proc = _launcher_cmd(launcher_path, ["list", "--json"], ienv, timeout=8.0)
    except (FileNotFoundError, OSError, subprocess.TimeoutExpired) as exc:
        _record(checks, "launcher-list", "fail", f"list failed: {exc}")
        return
    text = proc.stdout.decode("utf-8", errors="replace")
    try:
        listing = json.loads(text)
    except json.JSONDecodeError:
        listing = []
    if not isinstance(listing, list) or len(listing) < 80:
        _record(
            checks,
            "launcher-list",
            "fail",
            f"got {len(listing) if isinstance(listing, list) else 0} entries",
        )
        return
    _record(checks, "launcher-list", "pass", f"{len(listing)} hacks listed")

    # start a known hack and verify status running=true.
    try:
        proc = _launcher_cmd(
            launcher_path,
            ["start", "--hack", "blackhole_gles3", "--seconds", "30", "--force"],
            ienv,
            timeout=8.0,
        )
        rc_start = proc.returncode
    except (FileNotFoundError, OSError, subprocess.TimeoutExpired) as exc:
        _record(checks, "launcher-start", "fail", str(exc))
        rc_start = -1
    if rc_start == 0:
        _record(checks, "launcher-start", "pass", "start --force exit 0")
    else:
        _record(checks, "launcher-start", "fail", f"start returned {rc_start}")

    # status --json running:true
    try:
        proc = _launcher_cmd(launcher_path, ["status", "--json"], ienv, timeout=5.0)
        st = json.loads(proc.stdout.decode("utf-8", errors="replace"))
    except (
        FileNotFoundError,
        OSError,
        subprocess.TimeoutExpired,
        json.JSONDecodeError,
    ) as exc:
        st = {}
        _record(checks, "launcher-status", "fail", str(exc))
    if st.get("running") is True:
        _record(checks, "launcher-status", "pass", "running=true after start")
    else:
        _record(checks, "launcher-status", "fail", f"unexpected status: {st}")

    # A second start must report already-running.
    try:
        proc = _launcher_cmd(
            launcher_path,
            ["start", "--hack", "blackhole_gles3", "--seconds", "30"],
            ienv,
            timeout=5.0,
        )
    except (FileNotFoundError, OSError, subprocess.TimeoutExpired) as exc:
        proc = None  # type: ignore[assignment]
        _record(checks, "launcher-single-instance", "fail", str(exc))
    else:
        if proc.returncode != 0:
            _record(
                checks,
                "launcher-single-instance",
                "pass",
                f"second start refused (rc={proc.returncode})",
            )
        else:
            _record(
                checks,
                "launcher-single-instance",
                "fail",
                "second start succeeded; single-instance not enforced",
            )

    # stop -> exit 3 (not running) and no hack proc.
    _launcher_cmd(launcher_path, ["stop"], ienv, timeout=8.0)
    try:
        proc = _launcher_cmd(launcher_path, ["status"], ienv, timeout=5.0)
    except (FileNotFoundError, OSError, subprocess.TimeoutExpired) as exc:
        proc = None  # type: ignore[assignment]
        _record(checks, "launcher-stop", "fail", str(exc))
    else:
        if proc.returncode == 3:
            _record(
                checks,
                "launcher-stop",
                "pass",
                "status returned 3 (not running) after stop",
            )
        else:
            _record(
                checks,
                "launcher-stop",
                "fail",
                f"status rc={proc.returncode} after stop",
            )
    time.sleep(1.5)
    leftover = _hack_alive(os.path.join(HACK_BIN_DIR, "blackhole_gles3"))
    if leftover:
        _record(checks, "launcher-cleanup", "fail", "hack process still running")
    else:
        _record(checks, "launcher-cleanup", "pass", "no hack leftover")

    # Crash fallback: feed a fake hack directory holding one crashing script
    # and one symlink to the real blackhole_gles3.
    fake_dir = tempfile.mkdtemp(prefix="ncz-host-test-hacks-")
    crash_id = "ncz_test_crash"
    real_id = "ncz_test_real"
    crash_script = os.path.join(fake_dir, crash_id)
    real_link = os.path.join(fake_dir, real_id)
    pathlib.Path(crash_script).write_text("#!/bin/sh\nexit 1\n", encoding="utf-8")
    os.chmod(crash_script, 0o755)
    try:
        os.symlink(os.path.join(HACK_BIN_DIR, "blackhole_gles3"), real_link)
    except OSError:
        pass
    cfg_dir = ienv["XDG_CONFIG_HOME"]
    settings_path = os.path.join(cfg_dir, "dev.ncz", "screensaver", "settings.keyfile")
    pathlib.Path(settings_path).write_text(
        "\n".join(
            [
                "[dev.ncz.screensaver]",
                "mode=random",
                f"random-hacks=['{crash_id}', '{real_id}']",
                "cycle-delay=5",
                "hack-idle-delay=1",
            ]
        ),
        encoding="utf-8",
    )
    crash_env = dict(ienv)
    crash_env["NCZ_SCREENSAVER_DIRS"] = fake_dir
    crash_env["NCZ_SCREENSAVER_ALLOW_UNLISTED"] = "1"
    _launcher_cmd(launcher_path, ["stop"], crash_env, timeout=5.0)
    _launcher_cmd(
        launcher_path,
        ["start", "--mode", "random", "--seconds", "20", "--force"],
        crash_env,
        timeout=8.0,
    )
    # Give the supervisor time to skip the crash and land on the real hack.
    deadline = time.monotonic() + 12.0
    alive_real = False
    while time.monotonic() < deadline:
        if _hack_alive(real_link) or _hack_alive(
            os.path.join(HACK_BIN_DIR, "blackhole_gles3")
        ):
            alive_real = True
            break
        time.sleep(0.3)
    _launcher_cmd(launcher_path, ["stop"], crash_env, timeout=8.0)
    if alive_real:
        _record(
            checks,
            "launcher-crash-fallback",
            "pass",
            "supervisor skipped crash hack and ran real hack",
        )
    else:
        _record(
            checks,
            "launcher-crash-fallback",
            "fail",
            "real hack never became alive after crash",
        )

    # Playlist rotation with two real hacks and short cycle delay.
    playlist_env = _isolated_config_env()
    p_cfg = os.path.join(
        playlist_env["XDG_CONFIG_HOME"],
        "dev.ncz",
        "screensaver",
        "settings.keyfile",
    )
    pathlib.Path(p_cfg).write_text(
        "\n".join(
            [
                "[dev.ncz.screensaver]",
                "mode=playlist",
                "playlist=['hyprsaver_aurora_gles3', 'voronoi_gles3']",
                "cycle-delay=5",
                "hack-idle-delay=1",
                "verify-render=false",
            ]
        ),
        encoding="utf-8",
    )
    playlist_env["NCZ_SCREENSAVER_MIN_CYCLE"] = "1"
    _launcher_cmd(launcher_path, ["stop"], playlist_env, timeout=5.0)
    _launcher_cmd(
        launcher_path,
        ["start", "--mode", "playlist", "--seconds", "20", "--force"],
        playlist_env,
        timeout=8.0,
    )
    seen_hacks: set[str] = set()
    deadline = time.monotonic() + 20.0
    while time.monotonic() < deadline and len(seen_hacks) < 2:
        try:
            proc = _launcher_cmd(
                launcher_path, ["status", "--json"], playlist_env, timeout=4.0
            )
        except (FileNotFoundError, OSError, subprocess.TimeoutExpired):
            proc = None  # type: ignore[assignment]
        if proc is not None and proc.returncode == 0:
            try:
                st = json.loads(proc.stdout.decode("utf-8", errors="replace"))
            except json.JSONDecodeError:
                st = {}
            current = st.get("hack_id") or st.get("hack-id")
            if current:
                seen_hacks.add(current)
        time.sleep(0.5)
    _launcher_cmd(launcher_path, ["stop"], playlist_env, timeout=8.0)
    if len(seen_hacks) >= 2:
        _record(
            checks,
            "launcher-playlist",
            "pass",
            f"saw rotation: {sorted(seen_hacks)}",
        )
    else:
        _record(
            checks,
            "launcher-playlist",
            "fail",
            f"only saw {sorted(seen_hacks)} in playlist",
        )

    # set-timeout/set-hack/set-mode/set-color round-trips via config dump.
    round_env = _isolated_config_env()
    for setter, getter, value in [
        ("set-timeout", "hack-idle-delay", "42"),
        ("set-hack", "hack-id", "voronoi_gles3"),
        ("set-mode", "mode", "random"),
        ("set-color", "blackhole-color-mode", "kipthorne"),
    ]:
        try:
            proc = _launcher_cmd(
                launcher_path,
                [setter, value],
                round_env,
                timeout=5.0,
            )
        except (FileNotFoundError, OSError, subprocess.TimeoutExpired) as exc:
            _record(checks, f"set-{setter}", "fail", str(exc))
            continue
        if proc.returncode != 0:
            _record(
                checks,
                f"set-{setter}",
                "fail",
                proc.stderr.decode("utf-8", errors="replace")[:200],
            )
            continue
        # Read back via config dump.
        try:
            dump = _launcher_cmd(
                launcher_path, ["config", "dump"], round_env, timeout=5.0
            )
            cfg = json.loads(dump.stdout.decode("utf-8", errors="replace"))
        except (
            FileNotFoundError,
            OSError,
            subprocess.TimeoutExpired,
            json.JSONDecodeError,
        ) as exc:
            _record(checks, f"set-{setter}-readback", "fail", str(exc))
            continue
        if cfg.get(getter) == value:
            _record(
                checks,
                f"set-{setter}",
                "pass",
                f"{getter}={value} round-tripped",
            )
        else:
            _record(
                checks,
                f"set-{setter}",
                "fail",
                f"{getter}={cfg.get(getter)!r} expected {value!r}",
            )

    # kill -9 of the supervisor: no orphan hack within 5 s.
    kill_env = _isolated_config_env()
    _launcher_cmd(
        kill_env,
        ["start", "--hack", "blackhole_gles3", "--seconds", "60", "--force"],
        ienv,
        timeout=8.0,
    )
    # The launcher keeps its state in XDG_RUNTIME_DIR/ncz-screensaver/state.json.
    state_path = os.path.join(
        os.environ.get("XDG_RUNTIME_DIR", f"/run/user/{os.getuid()}"),
        SUPERVISOR_STATE,
    )
    sup_pid = 0
    if os.path.exists(state_path):
        try:
            st_data = json.loads(pathlib.Path(state_path).read_text(encoding="utf-8"))
            sup_pid = int(st_data.get("supervisor_pid", 0))
        except (OSError, ValueError, json.JSONDecodeError):
            sup_pid = 0
    if sup_pid > 0:
        with contextlib.suppress(ProcessLookupError, PermissionError):
            os.kill(sup_pid, signal.SIGKILL)
        time.sleep(1.0)
        leftover_hack = _hack_alive(os.path.join(HACK_BIN_DIR, "blackhole_gles3"))
        deadline = time.monotonic() + 4.0
        while time.monotonic() < deadline and leftover_hack:
            time.sleep(0.3)
            leftover_hack = _hack_alive(os.path.join(HACK_BIN_DIR, "blackhole_gles3"))
        if leftover_hack:
            _record(
                checks,
                "pdeathsig",
                "fail",
                "hack survived supervisor SIGKILL",
            )
        else:
            _record(
                checks,
                "pdeathsig",
                "pass",
                "hack died with supervisor (PDEATHSIG)",
            )
    else:
        _record(
            checks,
            "pdeathsig",
            "skip",
            f"could not locate supervisor pid in {state_path}",
        )


# ---------------------------------------------------------------------------
# Phase: idle.
# ---------------------------------------------------------------------------


def _idled_state_path() -> str:
    return os.path.join(
        os.environ.get("XDG_RUNTIME_DIR", f"/run/user/{os.getuid()}"),
        IDLED_STATE,
    )


def _read_idled_state() -> dict[str, Any]:
    path = _idled_state_path()
    if not os.path.exists(path):
        return {}
    try:
        return json.loads(pathlib.Path(path).read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {}


def _wait_for_idled_state(value: str, timeout: float) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if _read_idled_state().get("state") == value:
            return True
        time.sleep(0.1)
    return False


def _wait_for_running(launcher: str, env: dict[str, str], timeout: float) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            proc = _run([launcher, "status", "--json"], timeout=4.0, env=env)
        except (FileNotFoundError, OSError, subprocess.TimeoutExpired):
            proc = None  # type: ignore[assignment]
        if proc is not None and proc.returncode == 0:
            try:
                st = json.loads(proc.stdout.decode("utf-8", errors="replace"))
            except json.JSONDecodeError:
                st = {}
            if st.get("running") is True:
                return True
        time.sleep(0.2)
    return False


def phase_idle(
    results: dict[str, Any],
    checks: list[Check],
    env: dict[str, str],
    workdir: str,
    launcher_path: str,
    idled_path: str,
    no_dpms: bool,
    pw: str | None,
) -> None:
    if not os.path.exists(idled_path):
        _record(checks, "idled-binary", "fail", f"missing: {idled_path}")
        return
    if not os.path.exists(launcher_path):
        _record(checks, "launcher-binary", "fail", f"missing: {launcher_path}")
        return

    # Stop system-managed idled/idle-manager and remember to restore later.
    restore: list[tuple[str, str]] = []
    for svc in ("ncz-screensaver-idled.service", "ncz-idle-manager.service"):
        state = _read_unit_state(svc, env)
        if state == "active":
            try:
                _run(
                    ["systemctl", "--user", "stop", svc],
                    timeout=10.0,
                    env=env,
                )
            except (FileNotFoundError, OSError, subprocess.TimeoutExpired) as exc:
                _record(checks, f"stop-{svc}", "fail", str(exc))
                continue
            restore.append((svc, "start"))
    _record(
        checks,
        "pre-stop-services",
        "pass",
        f"services stopped, will restore {restore or 'none'}",
    )

    # Isolated config: mode=one, hack-id=hyprsaver_aurora_gles3,
    # hack-idle-delay=6, lock-enabled=false, display-off-delay=0.
    ienv = _isolated_config_env()
    cfg_dir = ienv["XDG_CONFIG_HOME"]
    settings_path = os.path.join(cfg_dir, "dev.ncz", "screensaver", "settings.keyfile")
    pathlib.Path(settings_path).write_text(
        "\n".join(
            [
                "[dev.ncz.screensaver]",
                "mode=one",
                "hack-id=hyprsaver_aurora_gles3",
                "hack-idle-delay=6",
                "lock-enabled=false",
                "display-off-delay=0",
                "verify-render=false",
            ]
        ),
        encoding="utf-8",
    )

    # --dry-run plan comparison.
    try:
        proc = _run([idled_path, "--dry-run"], timeout=10.0, env=ienv)
    except (FileNotFoundError, OSError, subprocess.TimeoutExpired) as exc:
        _record(checks, "idled-dry-run", "fail", str(exc))
    else:
        plan = proc.stdout.decode("utf-8", errors="replace")
        if "6" in plan and "one" in plan:
            _record(checks, "idled-dry-run", "pass", plan.splitlines()[0])
        else:
            _record(
                checks,
                "idled-dry-run",
                "fail",
                f"plan did not reflect config: {plan[:200]}",
            )

    # Run the daemon in the background; remember to kill it in finally.
    daemon: subprocess.Popen | None = None
    try:
        daemon = subprocess.Popen(
            [idled_path],
            env=ienv,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )
        time.sleep(1.5)
        if daemon.poll() is not None:
            output = (
                daemon.stdout.read().decode("utf-8", errors="replace")
                if daemon.stdout
                else ""
            )
            _record(
                checks, "idled-start", "fail", f"exited immediately: {output[:200]}"
            )
            return
        _record(checks, "idled-start", "pass", f"pid={daemon.pid}")

        # idled.json state should be "active".
        if _wait_for_idled_state("active", timeout=4.0):
            _record(checks, "idled-state-active", "pass", "state=active")
        else:
            _record(
                checks,
                "idled-state-active",
                "fail",
                f"state stuck at {_read_idled_state().get('state')!r}",
            )

        # Wait until saver runs (deadline = idle-delay + 6 s headroom).
        deadline = 6.0 + IDLE_DEADLINE_HEADROOM
        if _wait_for_running(launcher_path, ienv, deadline):
            _record(
                checks,
                "idled-saver-started",
                "pass",
                "launcher reports running after idle delay",
            )
        else:
            _record(
                checks,
                "idled-saver-started",
                "fail",
                "launcher never reported running",
            )
            # Bail out of further idle checks; daemon is still running and
            # will be cleaned up in the finally block.
            return

        # Non-black screenshot while the saver is up.
        saver_path = os.path.join(workdir, "shots", "idle_saver.ppm")
        ok, detail = _capture_grim(saver_path, 0.125, ienv)
        if ok:
            try:
                _, _, buf = hti.parse_ppm(pathlib.Path(saver_path).read_bytes())
                cov = hti.coverage_fraction(buf)
                if cov >= 0.02:
                    _record(
                        checks,
                        "idle-saver-frame",
                        "pass",
                        f"coverage={cov:.3f}",
                    )
                else:
                    _record(
                        checks,
                        "idle-saver-frame",
                        "fail",
                        f"coverage={cov:.3f} (looks black)",
                    )
            except (OSError, hti.PPMError) as exc:
                _record(checks, "idle-saver-frame", "fail", str(exc))
        else:
            _record(checks, "idle-saver-frame", "fail", detail)

        # Inject motion; saver must stop within 4 s.
        poke = os.environ.get("HT_WL_POKE", "")
        if poke:
            t0 = time.monotonic()
            try:
                _run([sys.executable, poke, "motion"], timeout=5.0, env=ienv)
            except (FileNotFoundError, OSError, subprocess.TimeoutExpired) as exc:
                _record(checks, "idle-motion", "fail", str(exc))
            stopped = False
            deadline = time.monotonic() + DISMISS_DEADLINE
            while time.monotonic() < deadline:
                try:
                    proc = _run(
                        [launcher_path, "status", "--json"], timeout=4.0, env=ienv
                    )
                    st = json.loads(proc.stdout.decode("utf-8", errors="replace"))
                except (
                    FileNotFoundError,
                    OSError,
                    subprocess.TimeoutExpired,
                    json.JSONDecodeError,
                ):
                    st = {}
                if not st.get("running"):
                    stopped = True
                    break
                time.sleep(0.2)
            latency = time.monotonic() - t0
            if stopped:
                _record(
                    checks,
                    "idle-motion-dismiss",
                    "pass",
                    f"stopped in {latency:.2f}s",
                    metrics={"latency_s": round(latency, 3)},
                )
            else:
                _record(
                    checks,
                    "idle-motion-dismiss",
                    "fail",
                    f"still running after {latency:.2f}s",
                    metrics={"latency_s": round(latency, 3)},
                )

            # Wait for the saver to run again, then test key dismissal.
            if _wait_for_running(launcher_path, ienv, 6.0 + IDLE_DEADLINE_HEADROOM):
                t0 = time.monotonic()
                try:
                    _run([sys.executable, poke, "key"], timeout=5.0, env=ienv)
                except (FileNotFoundError, OSError, subprocess.TimeoutExpired) as exc:
                    _record(checks, "idle-key", "fail", str(exc))
                stopped = False
                deadline = time.monotonic() + DISMISS_DEADLINE
                while time.monotonic() < deadline:
                    try:
                        proc = _run(
                            [launcher_path, "status", "--json"], timeout=4.0, env=ienv
                        )
                        st = json.loads(proc.stdout.decode("utf-8", errors="replace"))
                    except (
                        FileNotFoundError,
                        OSError,
                        subprocess.TimeoutExpired,
                        json.JSONDecodeError,
                    ):
                        st = {}
                    if not st.get("running"):
                        stopped = True
                        break
                    time.sleep(0.2)
                latency = time.monotonic() - t0
                if stopped:
                    _record(
                        checks,
                        "idle-key-dismiss",
                        "pass",
                        f"stopped in {latency:.2f}s",
                        metrics={"latency_s": round(latency, 3)},
                    )
                else:
                    _record(
                        checks,
                        "idle-key-dismiss",
                        "fail",
                        f"still running after {latency:.2f}s",
                        metrics={"latency_s": round(latency, 3)},
                    )

        # Idle inhibitor: start one, confirm the saver does NOT start, then
        # wait for the inhibitor to end and confirm it does start.
        if poke:
            try:
                proc = subprocess.Popen(
                    [sys.executable, poke, "inhibit", "--seconds", "25"],
                    env=ienv,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True,
                )
            except (FileNotFoundError, OSError) as exc:
                proc = None  # type: ignore[assignment]
                _record(checks, "idle-inhibit", "fail", str(exc))
            else:
                # Wait until wl_poke.py prints "inhibitor active".
                start_deadline = time.monotonic() + 8.0
                ready = False
                if proc.stdout is not None:
                    while time.monotonic() < start_deadline:
                        line = proc.stdout.readline()
                        if not line:
                            break
                        if "inhibitor active" in line:
                            ready = True
                            break
                if ready:
                    _record(
                        checks,
                        "idle-inhibit-active",
                        "pass",
                        "inhibitor reported active",
                    )
                else:
                    _record(
                        checks,
                        "idle-inhibit-active",
                        "fail",
                        "inhibitor never reported active",
                    )

                # Saver must NOT start while the inhibitor is up.
                if _wait_for_running(launcher_path, ienv, 6.0 + 8.0):
                    _record(
                        checks,
                        "idle-inhibit-blocks-saver",
                        "fail",
                        "saver started while inhibitor was active",
                    )
                else:
                    _record(
                        checks,
                        "idle-inhibit-blocks-saver",
                        "pass",
                        "saver did not start under inhibition",
                    )

                # Stop the inhibitor (kill the wl_poke process).
                with contextlib.suppress(ProcessLookupError):
                    proc.terminate()
                with contextlib.suppress(subprocess.TimeoutExpired):
                    proc.wait(timeout=4.0)
                with contextlib.suppress(ProcessLookupError):
                    proc.kill()

                # After the inhibitor ends, the saver should start.
                if _wait_for_running(launcher_path, ienv, 6.0 + 8.0):
                    _record(
                        checks,
                        "idle-inhibit-release",
                        "pass",
                        "saver started after inhibition ended",
                    )
                else:
                    _record(
                        checks,
                        "idle-inhibit-release",
                        "fail",
                        "saver did not start after inhibition ended",
                    )

        # Live reconfigure: change hack-idle-delay and verify the new plan.
        pathlib.Path(settings_path).write_text(
            "\n".join(
                [
                    "[dev.ncz.screensaver]",
                    "mode=one",
                    "hack-id=hyprsaver_aurora_gles3",
                    "hack-idle-delay=12",
                    "lock-enabled=false",
                    "display-off-delay=0",
                    "verify-render=false",
                ]
            ),
            encoding="utf-8",
        )
        try:
            _run(["gsettings", "recursively-relabel"], timeout=2.0, env=ienv)
        except (FileNotFoundError, OSError, subprocess.TimeoutExpired):
            pass
        # Trigger a keyfile reload by writing again -- gsettings watches mtime.
        time.sleep(1.5)
        st = _read_idled_state()
        plan = st.get("plan", {})
        if plan.get("hack_idle_delay") == 12:
            _record(
                checks,
                "idle-reconfigure",
                "pass",
                "plan updated to hack-idle-delay=12",
            )
        else:
            _record(
                checks,
                "idle-reconfigure",
                "fail",
                f"plan still shows {plan.get('hack_idle_delay')!r}",
            )

        # Lock chain: harmless lock script that touches a marker file.
        lock_dir = os.path.join(workdir, "fake-lock")
        os.makedirs(lock_dir, exist_ok=True)
        marker = os.path.join(lock_dir, "marker.txt")
        marker2 = os.path.join(lock_dir, "marker2.txt")
        lock_script = os.path.join(lock_dir, "lock.sh")
        pathlib.Path(lock_script).write_text(
            f"#!/bin/sh\ntouch {marker}\nsleep 1\ntouch {marker2}\n",
            encoding="utf-8",
        )
        os.chmod(lock_script, 0o755)
        pathlib.Path(settings_path).write_text(
            "\n".join(
                [
                    "[dev.ncz.screensaver]",
                    "mode=one",
                    "hack-id=hyprsaver_aurora_gles3",
                    "hack-idle-delay=3",
                    "lock-enabled=true",
                    "lock-delay=4",
                    "display-off-delay=0",
                    "verify-render=false",
                ]
            ),
            encoding="utf-8",
        )
        lock_env = dict(ienv)
        lock_env["NCZ_LOCK_CMD"] = lock_script
        # The daemon already running is using ienv; restart it with the new
        # env so NCZ_LOCK_CMD takes effect.
        with contextlib.suppress(ProcessLookupError):
            os.killpg(os.getpgid(daemon.pid), signal.SIGTERM)
        try:
            daemon.wait(timeout=5.0)
        except subprocess.TimeoutExpired:
            with contextlib.suppress(ProcessLookupError):
                os.killpg(os.getpgid(daemon.pid), signal.SIGKILL)
        daemon = subprocess.Popen(
            [idled_path],
            env=lock_env,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            start_new_session=True,
        )
        time.sleep(1.0)
        # Wait for saver to start (3 s delay + headroom).
        if _wait_for_running(launcher_path, lock_env, 3.0 + 6.0):
            # Then 4 s after that the lock marker must appear.
            deadline = time.monotonic() + 4.0 + 4.0
            while time.monotonic() < deadline:
                if os.path.exists(marker):
                    break
                time.sleep(0.2)
            if os.path.exists(marker):
                _record(
                    checks,
                    "idle-lock-chain",
                    "pass",
                    f"lock script ran at {time.ctime(os.path.getmtime(marker))}",
                )
            else:
                _record(
                    checks,
                    "idle-lock-chain",
                    "fail",
                    "lock marker never appeared",
                )
            # Saver must be stopped by the lock chain.
            time.sleep(2.0)
            try:
                proc = _run(
                    [launcher_path, "status", "--json"], timeout=4.0, env=lock_env
                )
                st = json.loads(proc.stdout.decode("utf-8", errors="replace"))
            except (
                FileNotFoundError,
                OSError,
                subprocess.TimeoutExpired,
                json.JSONDecodeError,
            ):
                st = {}
            if not st.get("running"):
                _record(
                    checks,
                    "idle-lock-stops-saver",
                    "pass",
                    "saver stopped before lock script",
                )
            else:
                _record(
                    checks,
                    "idle-lock-stops-saver",
                    "fail",
                    "saver still running while lock chain ran",
                )
        else:
            _record(
                checks,
                "idle-lock-chain",
                "fail",
                "saver never started for lock test",
            )

        # DPMS (unless --no-dpms).
        if not no_dpms:
            pathlib.Path(settings_path).write_text(
                "\n".join(
                    [
                        "[dev.ncz.screensaver]",
                        "mode=off",
                        "hack-idle-delay=3",
                        "lock-enabled=false",
                        "display-off-delay=14",
                        "verify-render=false",
                    ]
                ),
                encoding="utf-8",
            )
            dpms_env = dict(lock_env)
            with contextlib.suppress(ProcessLookupError):
                os.killpg(os.getpgid(daemon.pid), signal.SIGTERM)
            try:
                daemon.wait(timeout=5.0)
            except subprocess.TimeoutExpired:
                with contextlib.suppress(ProcessLookupError):
                    os.killpg(os.getpgid(daemon.pid), signal.SIGKILL)
            daemon = subprocess.Popen(
                [idled_path],
                env=dpms_env,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                start_new_session=True,
            )
            time.sleep(1.0)
            # 14 s + headroom before checking that outputs are off.
            time.sleep(14.0 + 2.0)
            dpms_shot = os.path.join(workdir, "shots", "dpms_off.ppm")
            ok_dpms, detail_dpms = _capture_grim(dpms_shot, 0.125, dpms_env)
            randr = _read_proc("wlr-randr", [])
            off_line = ""
            for line in randr.splitlines():
                if "enabled" in line and "no" in line:
                    off_line = line.strip()
                    break
            if (ok_dpms is False) or (off_line != ""):
                _record(
                    checks,
                    "idle-dpms-off",
                    "pass",
                    f"grim failed or outputs report off ({off_line or detail_dpms[:60]})",
                )
            else:
                # grim returned a frame: assume it's black.
                try:
                    _, _, buf = hti.parse_ppm(pathlib.Path(dpms_shot).read_bytes())
                    cov = hti.coverage_fraction(buf)
                except (OSError, hti.PPMError):
                    cov = -1.0
                if cov < 0.05:
                    _record(
                        checks,
                        "idle-dpms-off",
                        "pass",
                        f"grim returned black frame (cov={cov:.3f})",
                    )
                else:
                    _record(
                        checks,
                        "idle-dpms-off",
                        "fail",
                        f"outputs not off (cov={cov:.3f})",
                    )

            # Wake up with motion and verify the display returns.
            if poke:
                try:
                    _run([sys.executable, poke, "motion"], timeout=5.0, env=dpms_env)
                except (FileNotFoundError, OSError, subprocess.TimeoutExpired) as exc:
                    _record(checks, "idle-dpms-wake", "fail", str(exc))
                else:
                    deadline = time.monotonic() + 5.0
                    awake = False
                    while time.monotonic() < deadline:
                        ok2, _ = _capture_grim(
                            os.path.join(workdir, "shots", "dpms_on.ppm"),
                            0.125,
                            dpms_env,
                        )
                        if ok2:
                            try:
                                _, _, buf = hti.parse_ppm(
                                    pathlib.Path(
                                        os.path.join(workdir, "shots", "dpms_on.ppm")
                                    ).read_bytes()
                                )
                                cov = hti.coverage_fraction(buf)
                            except (OSError, hti.PPMError):
                                cov = 0.0
                            if cov >= 0.02:
                                awake = True
                                break
                        time.sleep(0.3)
                    if awake:
                        _record(
                            checks,
                            "idle-dpms-wake",
                            "pass",
                            "display returned to non-black",
                        )
                    else:
                        _record(
                            checks,
                            "idle-dpms-wake",
                            "fail",
                            "display did not return within 5 s",
                        )

    finally:
        # ALWAYS power outputs back on and stop the daemon.
        if daemon is not None and daemon.poll() is None:
            with contextlib.suppress(ProcessLookupError):
                os.killpg(os.getpgid(daemon.pid), signal.SIGTERM)
            try:
                daemon.wait(timeout=5.0)
            except subprocess.TimeoutExpired:
                with contextlib.suppress(ProcessLookupError):
                    os.killpg(os.getpgid(daemon.pid), signal.SIGKILL)
                daemon.wait(timeout=2.0)
        # Confirm outputs on (best-effort).
        randr_after = _read_proc("wlr-randr", [])
        if randr_after:
            _record(
                checks,
                "idle-outputs-restored",
                "pass",
                "wlr-randr answered after daemon stop",
            )

        # Restore services.
        for svc, action in restore:
            try:
                _run(
                    ["systemctl", "--user", action, svc],
                    timeout=10.0,
                    env=env,
                )
            except (FileNotFoundError, OSError, subprocess.TimeoutExpired):
                pass

    # Test the real user unit too. This is independent of the daemon above.
    try:
        _run(["systemctl", "--user", "daemon-reload"], timeout=10.0, env=env)
        _run(
            ["systemctl", "--user", "start", "ncz-screensaver-idled.service"],
            timeout=10.0,
            env=env,
        )
    except (FileNotFoundError, OSError, subprocess.TimeoutExpired) as exc:
        _record(checks, "user-unit-start", "fail", str(exc))
    else:
        deadline = time.monotonic() + 3.0
        active = False
        while time.monotonic() < deadline:
            if _read_unit_state("ncz-screensaver-idled.service", env) == "active":
                active = True
                break
            time.sleep(0.2)
        if active:
            _record(checks, "user-unit-active", "pass", "service is-active=active")
        else:
            _record(
                checks,
                "user-unit-active",
                "fail",
                "service did not become active within 3 s",
            )
        try:
            proc = _run(
                [
                    "journalctl",
                    "--user",
                    "-u",
                    "ncz-screensaver-idled",
                    "-n",
                    "20",
                    "--no-pager",
                ],
                timeout=10.0,
                env=env,
            )
            jlog = proc.stdout.decode("utf-8", errors="replace").strip()
            if jlog:
                pathlib.Path(workdir, "logs", "idled.journal.txt").write_text(
                    jlog, encoding="utf-8"
                )
                _record(checks, "user-unit-journal", "pass", "journal captured")
            else:
                _record(checks, "user-unit-journal", "skip", "no journal entries")
        except (FileNotFoundError, OSError, subprocess.TimeoutExpired) as exc:
            _record(checks, "user-unit-journal", "fail", str(exc))
        try:
            _run(
                ["systemctl", "--user", "stop", "ncz-screensaver-idled.service"],
                timeout=10.0,
                env=env,
            )
        except (FileNotFoundError, OSError, subprocess.TimeoutExpired):
            pass


# ---------------------------------------------------------------------------
# Phase: color.
# ---------------------------------------------------------------------------


def phase_color(
    results: dict[str, Any],
    checks: list[Check],
    env: dict[str, str],
    workdir: str,
    launcher_path: str,
) -> None:
    if not os.path.exists(launcher_path):
        _record(checks, "color-launcher", "fail", f"missing: {launcher_path}")
        return
    ienv = _isolated_config_env()
    cfg_dir = ienv["XDG_CONFIG_HOME"]
    settings_path = os.path.join(cfg_dir, "dev.ncz", "screensaver", "settings.keyfile")
    pathlib.Path(settings_path).write_text(
        "\n".join(
            [
                "[dev.ncz.screensaver]",
                "mode=one",
                "hack-id=blackhole_gles3",
                "hack-idle-delay=1",
                "lock-enabled=false",
                "display-off-delay=0",
                "verify-render=false",
                "blackhole-color-mode=stylized",
            ]
        ),
        encoding="utf-8",
    )

    samples: dict[str, dict[str, Any]] = {}
    unknown_seen = False
    for mode in ("stylized", "kipthorne", "faithful"):
        # set-color writes blackhole-color-mode into the keyfile.
        try:
            proc = _run(
                [launcher_path, "set-color", mode],
                timeout=5.0,
                env=ienv,
            )
        except (FileNotFoundError, OSError, subprocess.TimeoutExpired) as exc:
            _record(checks, f"color-{mode}", "fail", str(exc))
            continue
        if proc.returncode != 0:
            text = proc.stderr.decode("utf-8", errors="replace")
            if "unknown" in text or "fallback" in text.lower():
                unknown_seen = True
            _record(checks, f"color-set-{mode}", "fail", text[:200])
            continue
        # Start the saver, capture 4 s in.
        _run([launcher_path, "stop"], timeout=5.0, env=ienv)
        _run(
            [
                launcher_path,
                "start",
                "--hack",
                "blackhole_gles3",
                "--seconds",
                "20",
                "--force",
            ],
            timeout=8.0,
            env=ienv,
        )
        time.sleep(4.0)
        ppm = os.path.join(workdir, "shots", f"blackhole_{mode}.ppm")
        png = os.path.join(workdir, "shots", f"blackhole_{mode}.png")
        ok, detail = _capture_grim(ppm, 0.125, ienv)
        png_ok, _ = _screenshot_to_png(ppm, png, ienv, 0.25)
        _run([launcher_path, "stop"], timeout=5.0, env=ienv)
        if not ok:
            _record(checks, f"color-{mode}-capture", "fail", detail)
            continue
        try:
            _, _, buf = hti.parse_ppm(pathlib.Path(ppm).read_bytes())
            cov = hti.coverage_fraction(buf)
            r, g, b, n = hti.mean_brightness_of_bright_pixels(buf)
        except (OSError, hti.PPMError) as exc:
            _record(checks, f"color-{mode}-metrics", "fail", str(exc))
            continue
        samples[mode] = {
            "coverage": cov,
            "mean_r": r,
            "mean_g": g,
            "mean_b": b,
            "n_bright": n,
        }
        _record(
            checks,
            f"color-{mode}",
            "pass" if cov >= COLOR_COVERAGE_GATE else "fail",
            f"coverage={cov:.3f} mean=({r:.1f},{g:.1f},{b:.1f})",
            evidence=f"blackhole_{mode}.png" if png_ok else None,
            metrics=samples[mode],
        )

    if unknown_seen:
        _record(
            checks,
            "color-unknown-fallback",
            "pass",
            "launcher reported fallback for unknown mode",
        )

    # Pairwise comparison: every pair must differ.
    modes = list(samples.keys())
    pairs_ok = True
    for i in range(len(modes)):
        for j in range(i + 1, len(modes)):
            a = samples[modes[i]]
            b = samples[modes[j]]
            chroma = hti.chromaticity_distance(
                (a["mean_r"], a["mean_g"], a["mean_b"]),
                (b["mean_r"], b["mean_g"], b["mean_b"]),
            )
            bright_a = a["mean_r"] + a["mean_g"] + a["mean_b"]
            bright_b = b["mean_r"] + b["mean_g"] + b["mean_b"]
            br = hti.brightness_ratio(bright_a, bright_b)
            metrics = {"chroma": chroma, "brightness_ratio": br}
            if chroma < COLOR_CHROMA_MIN and br < COLOR_BRIGHTNESS_MIN:
                _record(
                    checks,
                    f"color-pair-{modes[i]}-{modes[j]}",
                    "fail",
                    f"chroma={chroma:.3f} brightness_ratio={br:.2f}",
                    metrics=metrics,
                )
                pairs_ok = False
            else:
                _record(
                    checks,
                    f"color-pair-{modes[i]}-{modes[j]}",
                    "pass",
                    f"chroma={chroma:.3f} brightness_ratio={br:.2f}",
                    metrics=metrics,
                )
    if pairs_ok:
        _record(checks, "color-pairs-distinct", "pass", "all pairs differ")
    else:
        _record(checks, "color-pairs-distinct", "fail", "some pair is identical")


# ---------------------------------------------------------------------------
# Phase: chooser.
# ---------------------------------------------------------------------------


def phase_chooser(
    results: dict[str, Any],
    checks: list[Check],
    env: dict[str, str],
    workdir: str,
    settings_path: str,
    launcher_path: str,
) -> None:
    if not os.path.exists(settings_path):
        _record(checks, "chooser-binary", "fail", f"missing: {settings_path}")
        return
    # --dump returns valid JSON with >= 80 catalog entries.
    try:
        proc = _run([settings_path, "--dump"], timeout=10.0, env=env)
    except (FileNotFoundError, OSError, subprocess.TimeoutExpired) as exc:
        _record(checks, "chooser-dump", "fail", str(exc))
        return
    text = proc.stdout.decode("utf-8", errors="replace")
    try:
        data = json.loads(text)
    except json.JSONDecodeError as exc:
        _record(checks, "chooser-dump", "fail", f"invalid JSON: {exc}")
        return
    if isinstance(data, list) and len(data) >= 80:
        _record(
            checks,
            "chooser-dump",
            "pass",
            f"{len(data)} catalog entries returned",
        )
    else:
        _record(
            checks,
            "chooser-dump",
            "fail",
            f"unexpected dump shape (type={type(data).__name__}, len={len(data) if hasattr(data, '__len__') else '?'})",
        )

    # --self-test, capture every PASS/FAIL line.
    ienv = _isolated_config_env()
    try:
        proc = subprocess.run(
            [settings_path, "--self-test"],
            capture_output=True,
            timeout=45.0,
            env=ienv,
        )
    except subprocess.TimeoutExpired:
        _record(checks, "chooser-selftest", "fail", "self-test timed out")
    else:
        out = proc.stdout.decode("utf-8", errors="replace")
        err = proc.stderr.decode("utf-8", errors="replace")
        log_path = os.path.join(workdir, "logs", "chooser_selftest.txt")
        pathlib.Path(log_path).write_text(
            out + "\n--- stderr ---\n" + err, encoding="utf-8"
        )
        pass_count = sum(1 for line in out.splitlines() if "PASS" in line)
        fail_count = sum(1 for line in out.splitlines() if "FAIL" in line)
        if fail_count == 0 and pass_count > 0:
            _record(
                checks,
                "chooser-selftest",
                "pass",
                f"{pass_count} PASS lines, 0 FAIL",
            )
        else:
            _record(
                checks,
                "chooser-selftest",
                "fail",
                f"{pass_count} PASS, {fail_count} FAIL",
                evidence=os.path.basename(log_path),
            )

    # Launch the GUI in the background and screenshot it.
    try:
        proc = subprocess.Popen(
            [settings_path],
            env=ienv,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            start_new_session=True,
        )
    except (FileNotFoundError, OSError) as exc:
        _record(checks, "chooser-launch", "fail", str(exc))
        proc = None  # type: ignore[assignment]
    if proc is not None:
        time.sleep(4.0)
        png = os.path.join(workdir, "shots", "settings.png")
        ok, detail = _screenshot_to_png(None, png, ienv, 0.5)
        if ok:
            _record(
                checks,
                "chooser-launch",
                "pass",
                f"screenshot -> {os.path.basename(png)}",
                evidence=os.path.basename(png),
            )
        else:
            _record(checks, "chooser-launch", "fail", detail)
        with contextlib.suppress(ProcessLookupError):
            os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
        try:
            proc.wait(timeout=5.0)
        except subprocess.TimeoutExpired:
            with contextlib.suppress(ProcessLookupError):
                os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
            proc.wait(timeout=2.0)

    # launcher list sanity (cheaper than re-running the launcher phase).
    if os.path.exists(launcher_path):
        try:
            proc = _run([launcher_path, "list"], timeout=5.0, env=env)
            lines = [ln for ln in proc.stdout.decode().splitlines() if ln.strip()]
            if len(lines) >= 80:
                _record(checks, "chooser-list-sanity", "pass", f"{len(lines)} hacks")
            else:
                _record(
                    checks,
                    "chooser-list-sanity",
                    "fail",
                    f"only {len(lines)} hacks listed",
                )
        except (FileNotFoundError, OSError, subprocess.TimeoutExpired) as exc:
            _record(checks, "chooser-list-sanity", "fail", str(exc))


# ---------------------------------------------------------------------------
# Result aggregation and atomic write.
# ---------------------------------------------------------------------------


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
    if "env" in phases:
        phase_env(results, checks_by_phase["env"], env)
    if "install" in phases:
        phase_install(results, checks_by_phase["install"], env, deb, pw, workdir)
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
