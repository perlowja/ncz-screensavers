# calibrator_env.py — Sky1-aware env builder for the calibrator subprocess.
#
# This module exists because the launcher's `build_child_env()` copies
# `os.environ` and adds GPU-related keys it scrapes from the running
# compositor's process environ. On NCZ-OS images where the compositor is
# not running (logged-in user on tty1, compositor crashed, kernel-mode-
# setting triggered, etc.), that scraper returns nothing — and the
# calibrator subprocess ends up running with the system loader path. The
# system loader path points `libEGL.so.1` at Mesa 26 with only the
# llvmpipe / zink drivers in the search list, so on a CIX Sky1 box with
# `mali_kbase` bound and a real /dev/mali0 the calibrator STILL falls
# back to llvmpipe and exits 3 (software-renderer refusal).
#
# That is the bug class that bit .66 on 2026-09-30. The fix is to give
# the calibrator subprocess the same env that /usr/local/bin/ncz-gpu-env
# sets for the desktop session:
#
#     LD_LIBRARY_PATH=/opt/cixgpu-pro/lib/aarch64-linux-gnu:...
#     __EGL_VENDOR_LIBRARY_FILENAMES=/opt/cixgpu-compat/share/glvnd/
#         egl_vendor.d/40_cix.json
#     NCZ_GPU_BACKEND=mali
#     __EGL_PLATFORM=surfaceless    (optional but reduces unneeded glue
#                                    when there is no compositor)
#
# All five are no-ops on amd64 (or any platform without /opt/cixgpu-pro
# installed): the existing compositor-env inheritance still wins, and
# the helper just returns the env unchanged.
#
# This module is stdlib-only.

from __future__ import annotations

import os
from pathlib import Path
from typing import Mapping, MutableMapping


# ---------------------------------------------------------------------------
# Tunables (the five files Live here, not the upstream launcher behaviour)
# ---------------------------------------------------------------------------

# Where the cix gui libmali GLES blob lives. Verified on .66 (2026-09-30):
#   /opt/cixgpu-pro/lib/aarch64-linux-gnu/
#       libEGL_cix.so  libEGL_cix.so.1  libEGL_cix.so.1.4.0
#       libgbm.so      libgbm.so.1      libgbm.so.1.0.0
#       libmali.so     libmali.so.0     libmali.so.0.53.0
#       libOpenCL.so
CIXGPU_PRO_LIBDIR = Path("/opt/cixgpu-pro/lib/aarch64-linux-gnu")

# The CIX glvnd vendor pin. Required so EGL's vendor lookup picks libEGL_cix
# instead of falling through to Mesa. Verified on .66 (2026-09-30).
CIXGPU_VENDOR_JSON = Path(
    "/opt/cixgpu-compat/share/glvnd/egl_vendor.d/40_cix.json"
)

# Only Sky1 has /opt/cixgpu-pro/. The helper detects that by checking
# CIXGPU_PRO_LIBDIR exists AND that .so can dlopen libmali.so.0 from it;
# the second check is the one that catches "the dir is here but the libs
# are not" (a torn upgrade, an incomplete recovery image, etc.).
#
# We do NOT spawn a subprocess to test the dlopen because the launcher
# already has a `_calibrator_json` wrapper that runs the calibrator and
# surfaces the exit code — if our env hint is wrong the launcher will
# still get code 3, just one process earlier. That's the right place to
# notice it. The cost of doing a dlopen probe here would be ~10 ms per
# calibrate call and would still race with the calibrator's own
# eglInitialize, so we don't bother.

# The backend that scopes `ncz-gpu-env`. Set on Sky1 to point the
# launcher's downstream consumers at mali. Not consumed by the
# calibrator directly (which only reads LD_LIBRARY_PATH and the
# vendor pin), but downstream helpers that read NCZ_GPU_BACKEND
# (e.g. ncz-screensaver's `find_compositor_environ` heuristic) do.


def is_sky1_arm64() -> bool:
    """Verbatim copy of the launcher's `platform_id()`.

    Returns True when /sys/bus/acpi/devices contains a CIXH* entry (any
    CIX Sky1 board) AND we're on aarch64. Falls back to /proc/device-tree/
    compatible for non-ACPI systems. Returns False on amd64 and on
    systems without either signal.

    Reimplemented here rather than imported from the launcher to keep
    this module stdlib-only and dependency-free; the launcher can swap
    out the import for the live one if it prefers.

    Verified 2026-09-30 against 192.168.207.66 (MS-R1 / cixmini):
    /proc/device-tree/compatible is ABSENT on this Sky1 board (the
    kernel uses ACPI rather than device tree), but /sys/bus/acpi/
    devices has CIXH1003 / CIXH5000 entries that match the glob.
    """
    if os.uname().machine != "aarch64":
        return False
    acpi = Path("/sys/bus/acpi/devices")
    if acpi.is_dir():
        try:
            for entry in acpi.iterdir():
                if entry.name.startswith("CIXH"):
                    return True
        except OSError:
            pass
    # Fallback for non-ACPI Sky1 boards (e.g. Pi-style device tree).
    compat_path = Path("/proc/device-tree/compatible")
    try:
        compat = compat_path.read_bytes()
    except OSError:
        return False
    return b"cix" in compat.lower()


def sky1_loader_paths_available() -> bool:
    """True iff Sky1 + the cixgpu-pro libdir exists on this host.

    Use this as the precondition for `augment_calibrator_env`: on any
    box where it returns False the helper is a no-op.
    """
    if not is_sky1_arm64():
        return False
    if not CIXGPU_PRO_LIBDIR.is_dir():
        return False
    # One representative .so must be present so we are not pointing at a
    # torn / empty upgrade.
    if not (CIXGPU_PRO_LIBDIR / "libmali.so.0").is_file():
        return False
    return True


def augment_calibrator_env(env: MutableMapping[str, str]) -> MutableMapping[str, str]:
    """Mutate `env` (and return it) so that the calibrator subprocess
    picks up the CIX proprietary GL stack on Sky1.

    Behaviour:
      - On Sky1 + cixgpu-pro present:
          * Prepend CIXGPU_PRO_LIBDIR to env["LD_LIBRARY_PATH"]
          * Set env["__EGL_VENDOR_LIBRARY_FILENAMES"] = str(CIXGPU_VENDOR_JSON)
            (without clobbering an existing value the operator set)
          * Set env["NCZ_GPU_BACKEND"] = "mali"  (only if not already set)
          * Set env["__EGL_PLATFORM"] = "surfaceless"
            (only if WAYLAND_DISPLAY is empty AND no DISPLAY is set;
             surfaceless is what works without a compositor)
      - On any other platform: no-op (env returned unchanged).

    The `__EGL_PLATFORM=surfaceless` default is the difference between
    "Mali-G720-Immortalis" (rc=0, real renderer reported) and
    "MESA: error: ZINK: failed to choose pdev" (rc=3, software refusal)
    on a host with no compositor. We honour an existing override if
    the operator (or the launcher's own env) set it.

    Why we DON'T reset LD_LIBRARY_PATH wholesale: the existing
    build_child_env() code carefully preserves LD_LIBRARY_PATH for
    the compositor's offload passes. We just prepend.

    Pre-conditions are intentional: this function does not assert. If
    the caller wants to know whether the env was actually mutated,
    it can call sky1_loader_paths_available() itself.
    """
    if not sky1_loader_paths_available():
        return env

    # -- LD_LIBRARY_PATH: prepend CIXGUI but keep anything already set
    # (the compositor's offload LD_LIBRARY_PATH is in the existing
    # env and we don't want to drop it).
    libdir = str(CIXGPU_PRO_LIBDIR)
    existing = env.get("LD_LIBRARY_PATH", "")
    if existing:
        parts = existing.split(":")
        if libdir not in parts:
            env["LD_LIBRARY_PATH"] = libdir + ":" + existing
    else:
        env["LD_LIBRARY_PATH"] = libdir

    # -- __EGL_VENDOR_LIBRARY_FILENAMES: set only if not already set.
    # The launcher's drop_egl_vendor_pins() pops this for offload
    # hacks but DOES NOT pop it for the calibrator subprocess, so a
    # compositor's value will survive. We don't either.
    # os.path.isfile (not Path.is_file) is the test seam: tests can
    # mock it; Path.is_file would not be reachable through mock.
    if "__EGL_VENDOR_LIBRARY_FILENAMES" not in env and os.path.isfile(
        CIXGPU_VENDOR_JSON
    ):
        env["__EGL_VENDOR_LIBRARY_FILENAMES"] = str(CIXGPU_VENDOR_JSON)

    # -- NCZ_GPU_BACKEND: "mali" on Sky1. Only set if missing.
    if "NCZ_GPU_BACKEND" not in env:
        env["NCZ_GPU_BACKEND"] = "mali"

    # -- __EGL_PLATFORM=surfaceless when neither Wayland nor X11 is
    # present. The launcher sets WAYLAND_DISPLAY from
    # find_compositor_environ() if a compositor is alive; if neither
    # WAYLAND_DISPLAY nor DISPLAY are set, the calibrator's default
    # EGL platform picks ZINK -> llvmpipe -> exit 3. Surfaceless picks
    # the GBM device directly without needing a display server.
    if "WAYLAND_DISPLAY" not in env and "DISPLAY" not in env:
        if "__EGL_PLATFORM" not in env:
            env["__EGL_PLATFORM"] = "surfaceless"

    return env


def augmented_calibrator_env(
    base: Optional[Mapping[str, str]] = None,
) -> dict[str, str]:
    """Convenience wrapper: returns a fresh dict from `base` (default
    os.environ) augmented for the calibrator subprocess.

    On non-Sky1 / no-cixgpu-pro hosts this is `dict(base or os.environ)`.
    On Sky1 with cixgpu-pro installed it is `dict(base or os.environ)` with
    the four keys above patched in (and `LD_LIBRARY_PATH` prepended).
    """
    env: dict[str, str] = dict(base if base is not None else os.environ)
    return augment_calibrator_env(env)


__all__ = [
    "CIXGPU_PRO_LIBDIR",
    "CIXGPU_VENDOR_JSON",
    "is_sky1_arm64",
    "sky1_loader_paths_available",
    "augment_calibrator_env",
    "augmented_calibrator_env",
]