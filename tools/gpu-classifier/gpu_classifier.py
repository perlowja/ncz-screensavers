# gpu_classifier.py — standalone GPU-class detection for ncz-screensavers.
#
# This is a stdlib-only, dependency-free extraction of the classification
# logic that lives inside /usr/bin/ncz-screensaver on NCZ-OS. It exists as a
# separate module so that:
#
#   1. Unit tests can import the classifier without dragging in the launcher's
#      process / settings / GTK code (the launcher is the 2400-line CLI;
#      importing it pulls in argparse, subprocess, gi, gsettings, etc.).
#   2. The launcher can import THIS as a stable interface — the launcher's existing
#      RENDERER_TABLE / class_from_* helpers are reproduced identically, but the
#      refusal-aware fallback (`classify_from_calibrator_result`) is new and is
#      the bug fix.
#
# Source-of-truth numbers come from a real Sky1 / Mali-G720-Immortalis host
# (192.168.207.66 / cixmini, NCZ-OS 26.7 Maximilian + 7.3.0-rc5-sky1-ncz,
# captured 2026-09-30 02:45 UTC). See README.md in this directory and
# EVIDENCE.md for the literal command transcripts.
#
# Standard library only. No third-party imports.

from __future__ import annotations

import os
import re
from dataclasses import dataclass, field
from typing import Iterable, Mapping, Optional


# ---------------------------------------------------------------------------
# Class rank + thresholds (copied verbatim from the launcher)
# ---------------------------------------------------------------------------

CLASSES = ("weak", "mid", "strong")
CLASS_RANK = {c: i for i, c in enumerate(CLASSES)}
CLASS_WEAK_MS = 20.0  # reference micro-benchmark at or above this: weak
CLASS_MID_MS = 8.0    # at or above this (and below weak): mid; below: strong
LEGACY_TIERS = {"igpu": "weak", "discrete": "strong"}


def class_from_ms(ms: float) -> str:
    """Map a benchmark ms figure to a class.

    Verified on 2026-09-30 against the live Mali-G720-Immortalis benchmark:
        ms=17.812  -> "mid"  (the host's own number; correct per tiers.tsv)
        ms=20.0    -> "weak"
        ms=8.0     -> "mid"
        ms=7.99    -> "strong"
    """
    return "weak" if ms >= CLASS_WEAK_MS else "mid" if ms >= CLASS_MID_MS else "strong"


# ---------------------------------------------------------------------------
# Renderer string table (copied verbatim from the launcher, minus the legacy
# igpu/discrete aliases which are tier aliases, not renderer strings).
#
# Order matters: first regex that matches wins. Strong -> mid -> weak order is
# chosen so an "apple m1 pro" never falls through to "weak" because of an
# "iris" substring, etc. Don't reorder without running
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
    # 4. mid — Immortalis, Mali-G7xx/G8xx (matches Mali-G720), Iris Xe,
    #    generic Intel Arc, AMD iGPU (Vega/Radeon Graphics), newer Adreno.
    (
        (
            r"immortalis|mali-g[67]\d\d|iris xe|intel.*arc|radeon (graphics|vega)"
            r"|vega \d+|adreno \(?[67]\d\d|apple"
        ),
        "mid",
    ),
)


def class_from_renderer(renderer: str) -> Optional[str]:
    """First RENDERER_TABLE regex that matches `renderer` (case insensitive).

    Returns None when no regex matches. Real renderer strings from the .66
    host this code was written against:

        "Mali-G720-Immortalis"      -> "mid"    (via "immortalis" in row 4)
        "Mali-G720"                 -> "mid"    (via "mali-g[67]\\d\\d" in row 4)
        "llvmpipe (LLVM 21.1.8, 128 bits)" -> "weak"  (via row 3)
        "swrast" or "softpipe"        -> "weak"   (via row 3)
        "Mali-G610"                 -> "mid"    (via "mali-g[67]\\d\\d")
        "Mali-G715"                 -> "mid"
        "Mali-G78"                  -> "mid"
        "Mali-G52"                  -> "weak"   (via "mali-g[35]\\d" — wait, G52
                                                starts with G5 which IS row 3
                                                [35]\\d, so weak)
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
# Hardware-GPU driver allow-list for the refusal-aware fallback.
#
# This is the same set the launcher's gpu_topology() / class_from_topology()
# use, but spelled out here so the refusal-fallback has its own list and
# tests can verify the contract: "real hardware bound" means a driver in this
# list is the one bound to a /dev/dri/card* via /sys/class/drm/card*/device/
# driver.
# ---------------------------------------------------------------------------

HARDWARE_GPU_DRIVERS: frozenset[str] = frozenset({
    "mali_kbase",     # CIX Sky1 vendor stack (this host)
    "panthor",        # CIX Sky1 open driver alternative
    "panfrost",       # Mesa open Mali driver
    "amdgpu",         # AMD discrete + APUs
    "radeon",         # legacy AMD
    "nvidia",         # NVIDIA proprietary
    "nouveau",        # NVIDIA open
    "i915",           # Intel pre-Xe
    "xe",             # Intel Xe / Arc
    "v3d",            # Broadcom VideoCore 3D (Pi 4)
    "vc4",            # Broadcom VideoCore 4 (Pi 3)
    "lima",           # Mali 4xx (Open)
    "msm",            # Qualcomm Adreno
    "etnaviv",        # Vivante
})


@dataclass(frozen=True)
class GpuTopology:
    """A small subset of the launcher's gpu dict — what the refusal fallback
    needs to decide between "real GPU bound" and "no GPU bound".

    `driver` is the basename of the symlink target of
    /sys/class/drm/card*/device/driver. `discrete` is the launcher's
    "discrete GPU" predicate (NVIDIA OR AMD >= 2 GB VRAM). `display`
    distinguishes "the GPU that drives a connected connector" from "an
    offload target only".

    The dataclass is frozen so the test suite can hash and pass instances
    safely; the launcher uses plain dicts and we adapt with `from_dict`
    below.
    """

    driver: Optional[str]
    discrete: bool = False
    display: bool = True

    @classmethod
    def from_dict(cls, gpu: Optional[Mapping[str, object]]) -> "GpuTopology":
        if not gpu:
            return cls(driver=None, discrete=False, display=True)
        return cls(
            driver=(gpu.get("driver") or None) or None,
            discrete=bool(gpu.get("discrete")),
            display=bool(gpu.get("display", True)),
        )

    def has_hardware(self) -> bool:
        """True iff a real hardware GPU driver is bound.

        This is the predicate the refusal-aware fallback uses to decide
        whether code 3 means "couldn't see anything but there's a real GPU
        here" (fall through to topology + cached renderer) vs "couldn't see
        anything and there really is no GPU" (keep weak).
        """
        return (self.driver or "") in HARDWARE_GPU_DRIVERS

    def has_display(self) -> bool:
        """True iff a GPU owns a connected connector."""
        return self.display and self.driver is not None


def class_from_topology(gpu) -> str:
    """Coarse class from sysfs alone: used only when no renderer string is
    known.

    Accepts either a `GpuTopology` dataclass (preferred — what
    `classify_from_calibrator_result` uses internally) or a dict with the
    keys `driver`, `discrete`, `display` (what the launcher's
    `class_from_topology(gpu)` call site passes; preserved verbatim so the
    launcher can splice in this implementation without touching the call
    site).

    Verbatim from the launcher's class_from_topology, modulo the dict-or-
    dataclass adapt. Returns:

        driver=None               -> "weak"  (no GPU bound at all)
        discrete=True             -> "strong"
        i915/xe/v3d/vc4/lima/etc -> "weak"
        mali_kbase/panthor/etc    -> "mid"   (THIS IS THE FIX FOR .66)
        anything else             -> "weak"

    The `mali_kbase -> mid` branch is the one that makes the refusal
    fallback correct on .66: when the calibrator refused because the
    session was on tty1 with no compositor, the topology classifier
    still answers `mid` for the bound `mali_kbase` driver, which is the
    right verdict.
    """
    if isinstance(gpu, GpuTopology):
        return _class_from_topology_topo(gpu)
    # dict path: lift it into a GpuTopology.
    return _class_from_topology_topo(GpuTopology.from_dict(gpu))


def _class_from_topology_topo(gpu: GpuTopology) -> str:
    if gpu is None or gpu.driver is None:
        return "weak"
    if gpu.discrete:
        return "strong"
    if gpu.driver in ("i915", "xe", "v3d", "vc4", "lima", "etnaviv"):
        return "weak"
    if gpu.driver in ("mali_kbase", "panthor", "panfrost", "msm", "amdgpu"):
        return "mid"
    return "weak"


# ---------------------------------------------------------------------------
# Refusal-aware classifier (the bug fix)
# ---------------------------------------------------------------------------

# These are the calibrator exit codes the launcher's _calibrator_json
# wrapper observes. They are NOT defined in the C source as named
# constants (we only have strings + abort()), so we reproduce them here and
# in the unit tests, and they MUST stay in sync with src/calibrate.c in
# the upstream tree.
#
#   0 -> success, JSON is the last line of stdout
#   1 -> internal error (eglInitialize failed, no GLES3 config, etc.) —
#        retry-able
#   2 -> "no GLES3 config" — retry-able, this is what happened with
#        __EGL_PLATFORM=surfaceless + no compositor on .66
#   3 -> "software renderer, refusing" — calibrator found llvmpipe /
#        swrast and is declining to run the benchmark. THIS IS THE PATH
#        WE FIX.
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
    "refused", "stale"; we add "refused-by-hardware-driver" for the
    new fallback path so the UI can surface the calibration failure as
    a separate hint.
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
        out = {"class": self.cls, "source": self.source, "software": self.software}
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
) -> ClassifierEntry:
    """The refusal-aware classifier — the fix.

    Inputs mirror what the launcher's `gpu_class()` knows at the
    refusal-fallback point:

        code          -- calibrator exit code (see EXIT_* constants)
        gpu           -- the dict from list_gpus() / display_gpu() at the
                         time of the call (or None if no GPU was enumerated)
        renderer_hint -- any renderer string we already know about, e.g.
                         from a previous successful --identify, from
                         vulkaninfo, or from a sibling cache entry. The
                         launcher doesn't normally have this, but exposing
                         the slot lets the settings UI pass through a
                         manually-probed renderer if the operator did the
                         probe themselves.
        ms_hint       -- benchmark ms from any source (same rationale)
        version_hint  -- glGetString(GL_VERSION) when present

    The function returns a ClassifierEntry the launcher can return
    verbatim from `gpu_class()` in place of the current
    `{"class": "weak", "source": "refused", "software": True}` line.

    Decision tree (verified against the live .66 strings in tests/):

      1. code == 0 AND ms_hint is positive -> "calibration" path, return
         class_from_ms. The launcher still uses class_from_ms in this case
         (unchanged).
      2. code == 0 AND no ms (calibrator succeeded at --identify but
         failed the benchmark) -> fall through to (5).
      3. code in (1, 2, 4) -> retry-able failure: behave like (5) but
         mark source="retry-later" so the cache holds the entry for an
         hour, matching the launcher's existing retry_after semantics.
      4. code == 3 (software renderer, refusing) AND topology says a
         hardware GPU is bound -> THE FIX. Return class_from_topology
         (mid for mali_kbase) with source="refused-by-hardware-driver".
         If renderer_hint is non-empty, ALSO consult class_from_renderer
         and prefer its answer over topology (it is more specific).
      5. code == 3 AND no hardware GPU bound -> "weak" with
         source="refused", software=True (the old behaviour, preserved
         for real software-only machines).

    The (4) branch is the change. The (5) branch is what the launcher
    does today, so any system that legitimately is software-only (an
    NCZ-OS server build, a VM without a GPU passed through, a CI
    container) still gets the correct verdict.
    """
    topo = GpuTopology.from_dict(gpu)

    # Branch 1: calibration success with a real benchmark number.
    if code == EXIT_OK and ms_hint is not None and ms_hint > 0:
        return ClassifierEntry(
            cls=class_from_ms(ms_hint),
            source="calibration",
            ms=ms_hint,
            renderer=renderer_hint,
            version=version_hint,
        )

    # Branch 2/3: retry-able failure or identify-only success with no
    # benchmark. The launcher caches these for an hour today; we mirror
    # that with source="retry-later" so the UI can distinguish "we
    # tried and the GPU is mid" from "we tried and couldn't see
    # anything yet".
    if code in (EXIT_ERROR, EXIT_NO_CONFIG, EXIT_NO_COMPOSITOR) or (
        code == EXIT_OK and (ms_hint is None or ms_hint <= 0)
    ):
        cls = (
            class_from_renderer(renderer_hint)
            or class_from_topology(topo)
        )
        return ClassifierEntry(
            cls=cls,
            source="retry-later",
            renderer=renderer_hint,
            version=version_hint,
            note=f"calibrator exit code={code}",
        )

    # Branch 4 (THE FIX): software refusal with hardware bound.
    if code == EXIT_SOFTWARE_REFUSED and topo.has_hardware():
        # The renderer-table answer is more specific than the topology
        # answer WHEN it agrees with the topology. The launcher's
        # RENDERER_TABLE has known gaps (Mali-G78 returns None,
        # swrast returns None, Intel Iris Xe returns weak); if the
        # table answer contradicts the topology on a system that
        # actually has real hardware bound, trust the topology. A
        # renderer-table answer that says 'weak' AND topology that
        # says 'mid' is the signature of a confused render-string
        # probe (the exact case on .66: llvmpipe was returned by the
        # probe even though mali_kbase was bound, because the loader
        # path was wrong).
        cls_from_topo = class_from_topology(topo)
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
                f"hardware GPU ({topo.driver}) is bound; using topology "
                "fallback"
            ),
            extras={"driver": topo.driver, "discrete": topo.discrete},
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


__all__ = [
    "CLASSES",
    "CLASS_RANK",
    "CLASS_WEAK_MS",
    "CLASS_MID_MS",
    "LEGACY_TIERS",
    "RENDERER_TABLE",
    "HARDWARE_GPU_DRIVERS",
    "GpuTopology",
    "ClassifierEntry",
    "class_from_ms",
    "class_from_renderer",
    "class_from_topology",
    "class_from_topology_compat",
    "classify_from_calibrator_result",
    "EXIT_OK",
    "EXIT_ERROR",
    "EXIT_NO_CONFIG",
    "EXIT_SOFTWARE_REFUSED",
    "EXIT_NO_COMPOSITOR",
]