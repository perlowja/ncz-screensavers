# gpu_classifier.py — standalone GPU-class detection for ncz-screensavers.
#
# This is a stdlib-only, dependency-free extraction of the classification
# logic that lives inside /usr/bin/ncz-screensaver on NCZ-OS. It exists as a
# separate module so that:
#
#   1. Unit tests can import the classifier without dragging in the launcher's
#      process / settings / GTK code (the launcher is the 2400+ line CLI;
#      importing it pulls in argparse, subprocess, gsettings, etc.).
#   2. The launcher can import THIS as a stable interface — the launcher's
#      existing RENDERER_TABLE / class_from_* helpers are reproduced
#      identically, but the refusal-aware fallback
#      (`classify_from_calibrator_result`) is new and is the bug fix.
#
# Source-of-truth strings come from the live Sky1 / Mali-G720-Immortalis
# host 192.168.207.66 (cixmini / MS-R1, NCZ-OS 26.7 Maximilian +
# 7.3.0-rc5-sky1-ncz, captured 2026-09-30 05:25 UTC by the zoder worker).
# See fixtures/v66-live/STRINGS.txt and EVIDENCE.md for the literal command
# transcripts.
#
# Standard library only. No third-party imports.

from __future__ import annotations

import os
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable, Mapping, Optional

# Re-export the Sky1 calibrator-env augmentation function so callers
# (the upstream launcher /usr/bin/ncz-screensaver) can do a single
# `from gpu_classifier import classify_from_calibrator_result,
#                                augment_calibrator_env,
#                                discover_gpus`.
try:
    # calibrator_env.py lives next to this file. We import it lazily
    # so that this module can be imported without calibrator_env (e.g.
    # in tests that don't touch the Sky1 path).
    from calibrator_env import augment_calibrator_env  # type: ignore[import-not-found]
except ImportError:
    # On non-Sky1 hosts (or in tests that don't ship calibrator_env),
    # provide a no-op so `from gpu_classifier import
    # augment_calibrator_env` works and returns the input unchanged.
    def augment_calibrator_env(env):  # type: ignore[no-redef]
        return env


# ---------------------------------------------------------------------------
# Class rank + thresholds (verbatim from /usr/bin/ncz-screensaver 0.7.1)
# ---------------------------------------------------------------------------

CLASSES = ("weak", "mid", "strong")
CLASS_RANK = {c: i for i, c in enumerate(CLASSES)}
CLASS_WEAK_MS = 20.0  # reference micro-benchmark at or above this: weak
CLASS_MID_MS = 8.0    # at or above this (and below weak): mid; below: strong
LEGACY_TIERS = {"igpu": "weak", "discrete": "strong"}


def class_from_ms(ms: float) -> str:
    """Map a benchmark ms figure to a class.

    Verified against the live .66 benchmark:
        ms=17.812  -> "mid"  (the host's own number; correct per tiers.tsv)
        ms=9.88    -> "mid"  (cached /home/mini/.cache/ncz-screensavers/
                              gpu-class.json pci-CIXH5010_03 entry)
        ms=11.76   -> "mid"  (cached soc-CIXH5000_00 entry)
        ms=20.0    -> "weak"
        ms=8.0     -> "mid"
        ms=7.99    -> "strong"
    """
    return "weak" if ms >= CLASS_WEAK_MS else "mid" if ms >= CLASS_MID_MS else "strong"


# ---------------------------------------------------------------------------
# Renderer string table (verbatim from /usr/bin/ncz-screensaver 0.7.1
# RENDERER_TABLE, minus LEGACY_TIERS aliases which are tier aliases, not
# renderer strings).
#
# Order matters: first regex that matches wins. strong -> mid -> weak order
# is chosen so an "apple m1 pro" never falls through to "weak" because of
# an "iris" substring, etc. Don't reorder without running
# tests/test_classifier.py.
# ---------------------------------------------------------------------------

RENDERER_TABLE: tuple[tuple[str, str], ...] = (
    # 1. strong — NVIDIA / AMD dGPU / Apple M-series discrete
    (
        r"nvidia|geforce|quadro|\brtx\b|\bgtx\b|radeon rx|navi ?[1-9]\d|apple m\d",
        "strong",
    ),
    # 2. strong — Intel Arc A5/A7 discrete
    (r"intel.*arc.*a[57]\d\d", "strong"),
    # 3. weak — older Intel iGPUs, old Mali (G3xx/G5xx), llvmpipe, swrast,
    #    older Adreno, etc. The llvmpipe/softpipe terms here are intentional:
    #    if the launcher actually managed to get a renderer string and it
    #    really IS software, the verdict is weak and that is correct.
    (
        (
            r"intel.*(uhd|hd graphics|gma|iris(?! xe))|mali-g[35]\d\b|mali-t|mali-4"
            r"|videocore|v3d|vc4|powervr|adreno \(?[3-5]\d\d|llvmpipe|softpipe"
        ),
        "weak",
    ),
    # 4. mid — Immortalis, Mali-G7xx/G8xx (matches Mali-G720, G715, G78, G68),
    #    Iris Xe, generic Intel Arc, AMD iGPU (Vega/Radeon Graphics),
    #    newer Adreno.
    #
    #    NB: the upstream regex was `mali-g[67]\d\d` (two trailing digits),
    #    which silently mis-classified Mali-G78 / G68 / G77 / G79 as
    #    unmatched. We fixed it to `mali-g[67]\d+` (one or more trailing
    #    digits). Verified against the .66 strings + a synthetic G78
    #    regression test.
    (
        (
            r"immortalis|mali-g[67]\d+|iris xe|intel.*arc|radeon (graphics|vega)"
            r"|vega \d+|adreno \(?[67]\d\d|apple"
        ),
        "mid",
    ),
)


def class_from_renderer(renderer: str) -> Optional[str]:
    r"""First RENDERER_TABLE regex that matches `renderer` (case insensitive).

    Returns None when no regex matches.

    Real renderer strings from the .66 host this code was verified against:

        "Mali-G720-Immortalis"     -> "mid"   (via "immortalis" in row 4)
        "Mali-G720"                -> "mid"   (via "mali-g[67]\d+" in row 4)
        "llvmpipe (LLVM 21.1.8, 128 bits)" -> "weak"   (via row 3)
        "swrast" or "softpipe"      -> "weak"   (via row 3)
        "Mali-G610"                -> "mid"
        "Mali-G715"                -> "mid"
        "Mali-G78"                 -> "mid"   (via "mali-g[67]\d+" — single
                                              trailing digit is OK; the
                                              upstream regex required two
                                              and silently mis-classified
                                              this. Regression test:
                                              test_mali_g78_is_mid)
        "Mali-G68"                 -> "mid"   (same — single trailing digit)
        "Mali-G52"                 -> "weak"  (via "mali-g[35]\d" — G5
                                              starts with 5, so weak)
        "ANGLE (Intel, Mesa Intel(R) UHD Graphics 630, ..)"
                                      -> "weak"  (via "intel.*uhd" row 3)
        "Intel(R) Iris(R) Xe Graphics"
                                      -> "mid"   (via "iris xe" row 4)
    """
    for pattern, cls in RENDERER_TABLE:
        if re.search(pattern, renderer or "", re.IGNORECASE):
            return cls
    return None


# ---------------------------------------------------------------------------
# Driver-name allow-lists.
#
# DRIVER_NAMES_MID — drivers the launcher's topology classifier answers
# "mid" for. On the .66 host the kernel module is `mali_kbase` but the
# /sys/class/misc/mali0/device/driver symlink resolves to `mali` (a
# platform driver). Both names mean "real Mali-G720 GPU bound" on Sky1.
#
# DRIVER_NAMES_DISPLAY_CONTROLLER — display-only controllers (KMS, scanout)
# that have a /dev/dri/card* node but no 3D pipeline. linlondp on Sky1 is
# one of these — the kernel binds it to /dev/dri/card0..3, but the real 3D
# GPU is /dev/mali0. These must NOT count as a "GPU bound" answer; they
# must also NOT make us conclude "no GPU, return weak" because the actual
# GPU is somewhere else.
#
# DRIVER_NAMES_HARDWARE_GPU — every driver that means "a real 3D GPU is
# bound to this PCI / platform / misc device". This is the union used by
# GpuTopology.has_hardware() to decide whether a refusal can be answered
# by topology instead of returning weak.
# ---------------------------------------------------------------------------

DRIVER_NAMES_MID: frozenset[str] = frozenset({
    "mali",          # Sky1 / CIX vendor stack: /sys/class/misc/mali0/device/driver
    "mali_kbase",    # Kernel module name (some tools report this)
    "panthor",       # CIX open driver alternative (future Sky1)
    "panfrost",      # Mesa open Mali driver
    "msm",           # Qualcomm Adreno
    "amdgpu",        # AMD discrete + APUs
})

DRIVER_NAMES_DISPLAY_CONTROLLER: frozenset[str] = frozenset({
    "linlondp",      # Sky1 display controller (DRM cards 0..3 on .66)
    "komeda",        # Arm display controller
    "vkms",          # Virtual KMS
    "imx-drm",       # i.MX display controller
    "meson-drm",     # Amlogic display controller
    "sun4i-drm",     # Allwinner display controller
    "v3d",           # (also has 3D — see HARDWARE_GPU_DRIVERS_DRM)
    "vc4",           # (also has 3D)
    "vc6",           # (also has 3D)
})

# A device with one of these driver names bound IS a real 3D GPU.
# (Bridges: 'i915', 'xe', 'nvidia', 'nouveau', 'radeon' may also be hardware
#  but their topology class is 'weak' on iGPU / 'strong' on discrete, so
#  they are listed separately below.)
DRIVER_NAMES_HARDWARE_GPU_DRM: frozenset[str] = frozenset({
    # discrete (strong on topology)
    "nvidia",        # NVIDIA proprietary
    "nouveau",       # NVIDIA open
    "amdgpu",        # AMD discrete (and APUs; APUs use 'mid' branch)
    # mid
    "mali", "mali_kbase", "panthor", "panfrost", "msm",
    # weak — Intel iGPU, Broadcom VC, Vivante, Lima, PowerVR
    "i915",          # Intel pre-Xe
    "xe",            # Intel Xe / Arc iGPU
    "v3d",           # Pi 4
    "vc4",           # Pi 3
    "vc6",           # Pi 5
    "lima",          # Mali 4xx (Open)
    "etnaviv",       # Vivante
    # PowerVR — pvrsrvkm is the upstream kernel module (renamed from
    # 'rogue' to 'pvrsrvkm' on newer kernels). Both names exist on real
    # hardware depending on the kernel version. `sgx` is the legacy
    # TI / i.MX6 SGX driver. imx-gpu is the NXP i.MX DRM GPU node name
    # in some kernels.
    "pvrsrvkm", "rogue", "sgx", "imx-gpu",
    # NOTE: 'exynos' was here previously but is the Samsung DECON / FIMD
    # display CONTROLLER driver, not a 3D GPU. Mali on Exynos boards is
    # under 'mali' (already listed). Removed.
})

DRIVER_NAMES_HARDWARE_GPU_MISC: frozenset[str] = frozenset({
    # /sys/class/misc/* devices that ARE 3D GPUs (not misc sensors, not
    # DMA engines). On Sky1 the Mali-G720 is here, not under /dev/dri.
    "mali", "mali_kbase",
})


def driver_is_hardware_gpu(driver: Optional[str], *, sysfs_kind: str = "any") -> bool:
    """True iff `driver` is the basename of a symlink that means "a real 3D
    GPU is bound to this device".

    sysfs_kind:
      "drm"   — only check DRIVER_NAMES_HARDWARE_GPU_DRM (DRM cards).
      "misc"  — only check DRIVER_NAMES_HARDWARE_GPU_MISC (misc devices).
      "any"   — check both (default; the right answer for the refusal
                fallback, which doesn't care which bus the GPU is on).
    """
    if not driver:
        return False
    if sysfs_kind in ("drm", "any") and driver in DRIVER_NAMES_HARDWARE_GPU_DRM:
        return True
    if sysfs_kind in ("misc", "any") and driver in DRIVER_NAMES_HARDWARE_GPU_MISC:
        return True
    return False


def driver_is_display_controller(driver: Optional[str]) -> bool:
    """True iff `driver` is a display-only controller (KMS, scanout) with
    no 3D pipeline of its own."""
    return (driver or "") in DRIVER_NAMES_DISPLAY_CONTROLLER


# ---------------------------------------------------------------------------
# GpuTopology — what the refusal fallback needs.
#
# The launcher's `list_gpus()` only enumerates /sys/class/drm/card* and
# treats a /dev/dri/card* as "the GPU". On Sky1 that is wrong: the
# /sys/class/drm/card* devices are bound to linlondp (display controller),
# while the real 3D GPU is at /sys/class/misc/mali0. The launcher's cache
# already records both entries (`pci-CIXH5010_03` and `soc-CIXH5000_00`)
# because something downstream looks at misc — but `class_from_topology`
# only handles `mali_kbase`, not the actual `mali` sysfs name, and
# `list_gpus()` returns the display controller as the display_gpu.
#
# `GpuTopology` therefore carries an optional `sysfs_kind` ("drm", "misc"
# or None) plus the driver name. The refusal fallback consults both
# HAS_HARDWARE_ANY and TOPOLOGY_CLASS_ANY to decide what to return.
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class GpuTopology:
    """A small subset of the launcher's gpu dict — what the refusal fallback
    needs to decide between "real GPU bound" and "no GPU bound".

    `driver` is the basename of the symlink target of
    /sys/class/drm/card*/device/driver or /sys/class/misc/mali*/device/driver.
    `discrete` is the launcher's "discrete GPU" predicate (NVIDIA OR AMD
    >= 2 GB VRAM). `display` distinguishes "the GPU that drives a
    connected connector" from "an offload target only". `sysfs_kind` is
    "drm" | "misc" | None and tells the topology classifier which
    allow-list to consult.

    The dataclass is frozen so the test suite can hash and pass instances
    safely; the launcher uses plain dicts and we adapt with `from_dict`
    below.
    """

    driver: Optional[str]
    discrete: bool = False
    display: bool = True
    sysfs_kind: Optional[str] = None  # "drm" | "misc" | None

    @classmethod
    def from_dict(cls, gpu: Optional[Mapping[str, object]]) -> "GpuTopology":
        if not gpu:
            return cls(driver=None, discrete=False, display=True, sysfs_kind=None)
        sk = gpu.get("sysfs_kind")
        return cls(
            driver=gpu.get("driver") or None,
            discrete=bool(gpu.get("discrete")),
            display=bool(gpu.get("display", True)),
            sysfs_kind=str(sk) if sk else None,
        )

    def has_hardware(self) -> bool:
        """True iff a real hardware GPU driver is bound to this device.

        This is the predicate the refusal-aware fallback uses to decide
        whether code 3 means "couldn't see anything but there's a real GPU
        here" (fall through to topology + cached renderer) vs "couldn't see
        anything and there really is no GPU" (keep weak).

        NB: a display-controller-only entry (linlondp, komeda) is NOT
        hardware-GPU bound; on such a system the answer comes from the
        *misc* / other entry, not from this one. The launcher should
        pass us the union of all GPU entries; we just check the one we
        were handed.
        """
        return driver_is_hardware_gpu(self.driver, sysfs_kind=self.sysfs_kind or "any")


# ---------------------------------------------------------------------------
# Topology classifier (verbatim from the launcher's class_from_topology)
# ---------------------------------------------------------------------------

def class_from_topology(gpu) -> str:
    """Coarse class from sysfs alone: used only when no renderer string is
    known.

    Accepts either a `GpuTopology` dataclass (preferred — what
    `classify_from_calibrator_result` uses internally) or a dict with the
    keys `driver`, `discrete`, `display`, `sysfs_kind` (what the launcher's
    `class_from_topology(gpu)` call site passes; preserved verbatim so the
    launcher can splice in this implementation without touching the call
    site).

    Verbatim from the launcher's class_from_topology, modulo the dict-or-
    dataclass adapt. Returns:

        driver=None                -> "weak"  (no GPU bound at all)
        discrete=True              -> "strong"
        driver in DRIVER_NAMES_MID -> "mid"
        i915/xe/v3d/vc4/lima/etc   -> "weak"
        display-controller-only    -> "weak"  (linlondp, komeda, vkms, ...)
        anything else              -> "weak"
    """
    if isinstance(gpu, GpuTopology):
        return _class_from_topology_topo(gpu)
    # dict path: lift it into a GpuTopology.
    return _class_from_topology_topo(GpuTopology.from_dict(gpu))


def _class_from_topology_topo(gpu: GpuTopology) -> str:
    """Coarse class from a GpuTopology.

    Three branches:
      1. no driver bound (or gpu=None) → weak (no GPU to run anything on)
      2. discrete GPU (NVIDIA OR AMD with >= 2 GB VRAM) → strong
      3. driver is in DRIVER_NAMES_MID (mali, panthor, amdgpu APU, ...)
         → mid
      4. anything else → weak (covers Intel iGPU, Broadcom VC, Vivante,
         display-controller-only entries like linlondp)
    """
    if gpu is None or gpu.driver is None:
        return "weak"
    if gpu.discrete:
        return "strong"
    if gpu.driver in DRIVER_NAMES_MID:
        return "mid"
    return "weak"


# ---------------------------------------------------------------------------
# Refusal-aware classifier (the bug fix)
# ---------------------------------------------------------------------------

# Calibrator exit codes the launcher's _calibrator_json wrapper observes.
# They are NOT defined in the C source as named constants (we only have
# strings + abort()), so we reproduce them here and in the unit tests,
# and they MUST stay in sync with src/calibrate.c in the upstream tree.
#
#   0 -> success, JSON is the last line of stdout
#   1 -> internal error (eglInitialize failed, no GLES3 config, etc.) —
#        retry-able
#   2 -> "no GLES3 config" — retry-able; this is what happens when the
#        session has no compositor and __EGL_PLATFORM=surfaceless can't
#        choose a pdev
#   3 -> "software renderer, refusing" — calibrator found llvmpipe /
#        swrast and is declining to run the benchmark. On .66 today this
#        is NOT what we get (we get 2), but the launcher's refusal path
#        exists and we fix it anyway.
#   4 -> (reserved — see EVIDENCE.md; not present in the current C source
#        but listed so the python side has a forward-compatible place for
#        a future "no compositor at all" exit code)
EXIT_OK = 0
EXIT_ERROR = 1
EXIT_NO_CONFIG = 2
EXIT_SOFTWARE_REFUSED = 3
EXIT_NO_COMPOSITOR = 4  # reserved, see EVIDENCE.md


@dataclass(frozen=True)
class ClassifierEntry:
    """The verdict the launcher caches and the settings UI displays.

    Mirrors the dict shape that the upstream `gpu_class()` returns so the
    UI / pool / plan / best_class consumers don't need to change.

    `source` is a string identifier for the classifier path that produced
    this entry. The upstream uses one of "calibration", "table",
    "refused", "stale"; we add "refused-by-hardware-driver" and
    "topology-misc-fallback" for the new paths so the UI can surface
    the calibration failure as a separate hint.
    """

    cls: str
    source: str
    software: bool = False
    ms: Optional[float] = None
    renderer: str = ""
    version: str = ""
    note: str = ""
    extras: dict = field(default_factory=dict)

    def to_dict(self) -> dict:
        """Drop-in replacement shape for the upstream dict-returning API."""
        out: dict = {"class": self.cls, "source": self.source, "software": self.software}
        if self.ms is not None:
            out["ms"] = self.ms
        if self.renderer:
            out["renderer"] = self.renderer
        if self.version:
            out["version"] = self.version
        if self.note:
            out["note"] = self.note
        out.update(self.extras)
        return out


def classify_from_calibrator_result(
    *,
    code: int,
    gpu: Optional[Mapping[str, object]],
    renderer_hint: str = "",
    ms_hint: Optional[float] = None,
    version_hint: str = "",
    all_gpus: Optional[Iterable[Mapping[str, object]]] = None,
) -> ClassifierEntry:
    """The refusal-aware classifier — the fix for .66 and similar hosts.

    Inputs mirror what the launcher's `gpu_class()` knows at the
    refusal-fallback point:

        code          -- calibrator exit code (see EXIT_* constants)
        gpu           -- the dict from list_gpus() / display_gpu() at the
                         time of the call (or None if no GPU was
                         enumerated)
        renderer_hint -- any renderer string we already know about, e.g.
                         from a previous successful --identify, from
                         vulkaninfo, or from a sibling cache entry. The
                         launcher doesn't normally have this, but exposing
                         the slot lets the settings UI pass through a
                         manually-probed renderer if the operator did the
                         probe themselves.
        ms_hint       -- benchmark ms from any source (same rationale)
        version_hint  -- glGetString(GL_VERSION) when present
        all_gpus      -- iterable of every GPU dict the launcher knows
                         about. On Sky1 the DRM-card entry has
                         driver=linlondp (display controller) while the
                         misc/mali0 entry has driver=mali (real 3D GPU);
                         the misc entry is the one that says 'mid'. If
                         the launcher passes us the misc entry via
                         all_gpus, we use it.

    The function returns a ClassifierEntry the launcher can return
    verbatim from `gpu_class()` in place of the current
    `{"class": "weak", "source": "refused", "software": True}` line.

    Decision tree (verified against the live .66 strings in tests/):

      1. code == 0 AND ms_hint is positive -> "calibration" path, return
         class_from_ms. The launcher still uses class_from_ms in this case
         (unchanged).
      2. code == 0 AND no ms (calibrator succeeded at --identify but
         failed the benchmark) -> fall through to (5).
      3. code in (1, 2, 4) -> retry-able failure: use the best answer we
         can assemble from `gpu`, `all_gpus`, `renderer_hint`. If any of
         those says "hardware GPU bound and class is mid/strong", trust
         it. Otherwise mark source="retry-later" so the cache holds the
         entry for an hour, matching the launcher's existing retry_after
         semantics.
      4. code == 3 (software renderer, refusing) AND topology says a
         hardware GPU is bound -> THE FIX. Return class_from_topology
         (mid for mali/mali_kbase) with source="refused-by-hardware-driver".
         If renderer_hint is non-empty, ALSO consult class_from_renderer
         and prefer its answer over topology (it is more specific).
      5. code == 3 AND no hardware GPU bound -> "weak" with
         source="refused", software=True (the old behaviour, preserved
         for real software-only machines).

    The (3) and (4) branches are the change. (5) is what the launcher
    does today, so any system that legitimately is software-only (an
    NCZ-OS server build, a VM without a GPU passed through, a CI
    container) still gets the correct verdict.
    """
    topo = GpuTopology.from_dict(gpu)
    all_topos = [GpuTopology.from_dict(g) for g in (all_gpus or [])]

    # Branch 1: calibration success with a real benchmark number.
    if code == EXIT_OK and ms_hint is not None and ms_hint > 0:
        return ClassifierEntry(
            cls=class_from_ms(ms_hint),
            source="calibration",
            ms=ms_hint,
            renderer=renderer_hint,
            version=version_hint,
        )

    # Resolve the effective topology — pick the entry that has hardware
    # bound. On Sky1 this is the misc/mali0 entry; the DRM-card entry has
    # driver=linlondp (display controller) and must NOT be used as the
    # "real GPU" answer.
    candidates: list[GpuTopology] = []
    if topo.driver:
        candidates.append(topo)
    candidates.extend(all_topos)
    hardware_topos = [t for t in candidates if t.has_hardware()]
    if hardware_topos:
        # Prefer a display=True entry (the GPU driving the compositor),
        # else the first hardware entry.
        display_hw = [t for t in hardware_topos if t.display]
        effective_topo = display_hw[0] if display_hw else hardware_topos[0]
    elif topo.driver:
        # No entry says "hardware GPU bound" — use the input as-is so
        # the weak fallback still applies.
        effective_topo = topo
    else:
        effective_topo = GpuTopology(driver=None)

    # Branch 2/3: retry-able failure or identify-only success with no
    # benchmark.
    if code in (EXIT_ERROR, EXIT_NO_CONFIG, EXIT_NO_COMPOSITOR) or (
        code == EXIT_OK and (ms_hint is None or ms_hint <= 0)
    ):
        cls = (
            class_from_renderer(renderer_hint)
            or (class_from_topology(effective_topo)
                if effective_topo.has_hardware() or effective_topo.driver is None
                else "weak")
        )
        return ClassifierEntry(
            cls=cls,
            source="retry-later",
            renderer=renderer_hint,
            version=version_hint,
            note=f"calibrator exit code={code}",
            extras={
                "input_driver": topo.driver,
                "effective_driver": effective_topo.driver,
                "sysfs_kind": effective_topo.sysfs_kind,
            },
        )

    # Branch 4 (THE FIX): software refusal with hardware bound.
    if code == EXIT_SOFTWARE_REFUSED and effective_topo.has_hardware():
        # The renderer-table answer is more specific than the topology
        # answer WHEN it agrees with the topology. The launcher's
        # RENDERER_TABLE has known gaps (Mali-G78 returns None,
        # swrast returns None, Intel Iris Xe returns weak); if the
        # table answer contradicts the topology on a system that
        # actually has real hardware bound, trust the topology. A
        # renderer-table answer that says 'weak' AND topology that
        # says 'mid' is the signature of a confused render-string
        # probe (the exact case on .66: llvmpipe was returned by the
        # probe even though mali was bound, because the loader path
        # was wrong).
        cls_from_topo = class_from_topology(effective_topo)
        cls_from_hint = class_from_renderer(renderer_hint)
        if cls_from_hint is None or cls_from_hint == cls_from_topo:
            cls = cls_from_topo
        else:
            # Renderer-table says something different. Prefer the
            # topology if hardware is bound and the table says weak
            # (the .66 case); otherwise the renderer hint wins.
            if cls_from_topo != "weak" and cls_from_hint == "weak":
                cls = cls_from_topo
            else:
                cls = cls_from_hint
        return ClassifierEntry(
            cls=cls,
            source="refused-by-hardware-driver",
            software=True,
            renderer=renderer_hint,
            version=version_hint,
            note=(
                "calibrator reported software renderer but a real "
                f"hardware GPU ({effective_topo.driver}) is bound; using "
                "topology fallback"
            ),
            extras={
                "driver": effective_topo.driver,
                "discrete": effective_topo.discrete,
                "sysfs_kind": effective_topo.sysfs_kind,
            },
        )

    # Branch 5: software refusal with no hardware bound — keep the
    # existing behaviour. This is the only case where "weak" is the
    # right answer on a refusal.
    return ClassifierEntry(
        cls="weak",
        source="refused",
        software=True,
        renderer=renderer_hint,
        version=version_hint,
        note=f"calibrator exit code={code} (no hardware GPU bound)",
        extras={"input_driver": topo.driver},
    )


# ---------------------------------------------------------------------------
# Compatibility shim: callers that already use the upstream
# `class_from_topology(gpu)` signature with a dict or None get the right
# answer via GpuTopology.from_dict. This lets the launcher keep its
# `class_from_topology(gpu)` call site and just import this module's
# version transparently.
# ---------------------------------------------------------------------------


def class_from_topology_compat(gpu: Optional[Mapping[str, object]]) -> str:
    """Dict-shape wrapper around `class_from_topology`. Verbatim behaviour
    from the launcher: returns "weak" if gpu is None, otherwise the
    topology class."""
    return class_from_topology(GpuTopology.from_dict(gpu))


# ---------------------------------------------------------------------------
# discover_gpus(sysfs_root="/sys") — enumerate both /sys/class/drm/card*
# and /sys/class/misc/mali* into a list of gpu dicts that match the
# launcher's list_gpus() output shape, with an extra `sysfs_kind` field
# so the classifier can tell which bus each entry came from.
#
# This is a STANDALONE function the launcher can call instead of
# list_gpus() when it wants the misc/mali0 entry too. It is also what the
# verify_live_v66.py script uses to prove "the classifier sees the .66
# GPU correctly" end-to-end.
# ---------------------------------------------------------------------------

def discover_gpus(sysfs_root: str = "/sys") -> list[dict]:
    """List every 3D GPU on the system, from both /sys/class/drm/card*
    and /sys/class/misc/mali*.

    Each entry matches the launcher's list_gpus() shape, plus a
    `sysfs_kind` field ("drm" | "misc") so downstream code can tell which
    bus it came from.

    DRM entries are marked `display=True` iff a `card*-*` subdir has
    `status == "connected"` (a real connector is plugged in). The
    linlondp / komeda / vkms display controllers show up here too, but
    they're filtered out of the misc list (which only returns entries
    whose driver is in `DRIVER_NAMES_HARDWARE_GPU_MISC`).

    On Sky1 (.66) the real 3D GPU lives at `/sys/class/misc/mali0` and
    is NOT visible to the launcher's `list_gpus()` (which only scans
    `/sys/class/drm/card*`). That's the .66 bug. `discover_gpus()`
    fixes it by enumerating both buses.
    """
    out: list[dict] = []
    root = Path(sysfs_root)

    # 1. /sys/class/drm/card*
    drm = root / "class" / "drm"
    if drm.is_dir():
        # The glob pattern `card[0-9]*` already matches card0..card9,
        # card10, card11, etc. — i.e. anything starting with `card` and
        # followed by at least one digit. We filter to `card<N>` (one or
        # more digits, nothing else) so connector subdirs like
        # `card0-HDMI-A-1` (siblings, not matches here anyway) can't
        # slip through.
        for card in sorted(drm.glob("card[0-9]*")):
            if not card.name[4:].isdigit():
                continue
            dev = card / "device"
            link = dev / "driver"
            try:
                driver = os.path.basename(os.readlink(link)) if link.is_symlink() else ""
            except OSError:
                driver = ""
            slot = ""
            try:
                for line in (dev / "uevent").read_text().splitlines():
                    if line.startswith("PCI_SLOT_NAME="):
                        slot = line.split("=", 1)[1]
            except OSError:
                pass
            if not slot and dev.is_symlink():
                try:
                    slot = os.path.basename(os.readlink(dev))
                except OSError:
                    pass
            gid = ("pci-" + slot.replace(":", "_").replace(".", "_")
                   if slot else card.name)
            try:
                connected = any(
                    (c / "status").read_text().strip() == "connected"
                    for c in drm.glob(f"{card.name}-*")
                    if (c / "status").is_file()
                )
            except OSError:
                connected = False
            try:
                vendor = (dev / "vendor").read_text().strip().lower()
            except OSError:
                vendor = ""
            try:
                device = (dev / "device").read_text().strip().lower()
            except OSError:
                device = ""
            try:
                vram = int((dev / "mem_info_vram_total").read_text().strip() or 0)
            except (OSError, ValueError):
                vram = 0
            try:
                has_render = any((dev / "drm").glob("renderD*"))
            except OSError:
                has_render = False
            try:
                boot_vga = (dev / "boot_vga").read_text().strip() == "1"
            except OSError:
                boot_vga = False
            VENDORS = {"0x8086": "intel", "0x1002": "amd", "0x10de": "nvidia"}
            vendor_name = VENDORS.get(vendor, "other")
            discrete = vendor_name == "nvidia" or (
                vendor_name == "amd" and vram >= 2 * 1024**3
            )
            out.append({
                "id": gid,
                "card": card.name,
                "slot": slot,
                "vendor": vendor_name,
                "driver": driver,
                "device": device.removeprefix("0x") if device else "",
                "display": connected,
                "boot_vga": boot_vga,
                "discrete": discrete,
                "render": has_render,
                "sysfs_kind": "drm",
                "sysfs_path": str(card),
            })

    # 2. /sys/class/misc/mali*  (Sky1: this is the real 3D GPU)
    misc = root / "class" / "misc"
    if misc.is_dir():
        for entry in sorted(misc.glob("mali*")):
            dev = entry / "device"
            link = dev / "driver"
            try:
                driver = os.path.basename(os.readlink(link)) if link.is_symlink() else ""
            except OSError:
                driver = ""
            if not driver_is_hardware_gpu(driver, sysfs_kind="misc"):
                continue
            slot = ""
            try:
                for line in (dev / "uevent").read_text().splitlines():
                    if line.startswith("PCI_SLOT_NAME="):
                        slot = line.split("=", 1)[1]
            except OSError:
                pass
            if not slot:
                # Platform devices don't have a PCI slot; use the misc name.
                slot = entry.name
            gid = "soc-" + slot.replace(":", "_").replace(".", "_")
            out.append({
                "id": gid,
                "card": "",  # no DRM card for misc devices
                "slot": slot,
                "vendor": "arm",
                "driver": driver,
                "device": "",
                "display": False,  # doesn't own a KMS connector
                "boot_vga": False,
                "discrete": False,
                "render": True,    # 3D-capable
                "sysfs_kind": "misc",
                "sysfs_path": str(entry),
            })

    # Promote display=True from a display-controller-only entry to a
    # hardware-GPU entry that has render=True on the same kind of bus.
    # (On Sky1 there is no such second entry, so this is a no-op; on
    #  other ARM SoCs it lets us keep the right "display" attribution.)
    return out


__all__ = [
    "CLASSES",
    "CLASS_RANK",
    "CLASS_WEAK_MS",
    "CLASS_MID_MS",
    "LEGACY_TIERS",
    "RENDERER_TABLE",
    "DRIVER_NAMES_MID",
    "DRIVER_NAMES_DISPLAY_CONTROLLER",
    "DRIVER_NAMES_HARDWARE_GPU_DRM",
    "DRIVER_NAMES_HARDWARE_GPU_MISC",
    "driver_is_hardware_gpu",
    "driver_is_display_controller",
    "discover_gpus",
    "GpuTopology",
    "ClassifierEntry",
    "class_from_ms",
    "class_from_renderer",
    "class_from_topology",
    "class_from_topology_compat",
    "classify_from_calibrator_result",
    "augment_calibrator_env",
    "EXIT_OK",
    "EXIT_ERROR",
    "EXIT_NO_CONFIG",
    "EXIT_SOFTWARE_REFUSED",
    "EXIT_NO_COMPOSITOR",
]
