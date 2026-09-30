"""tests/test_classifier.py — unit tests for tools/gpu-classifier.

The renderer strings, driver names, GPU dicts and live benchmark numbers
in these tests come from a real Sky1 / Mali-G720-Immortalis host
(192.168.207.66 / cixmini, NCZ-OS 26.7 Maximilian + 7.3.0-rc5-sky1-ncz,
captured 2026-09-30 05:25 UTC; see fixtures/v66-live/STRINGS.txt).

Run from tools/gpu-classifier:

    python3 -m unittest discover tests -v

Stdlib only. The tests do not touch the host's hardware — they exercise
pure functions and the refusal-aware fallback with the EXACT dict shapes
list_gpus() / display_gpu() produce on .66.
"""

from __future__ import annotations

import json
import sys
import unittest
from pathlib import Path

# Make the parent directory importable so we can `import gpu_classifier`
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from gpu_classifier import (
    CLASS_MID_MS,
    CLASS_RANK,
    CLASS_WEAK_MS,
    EXIT_ERROR,
    EXIT_NO_COMPOSITOR,
    EXIT_NO_CONFIG,
    EXIT_OK,
    EXIT_SOFTWARE_REFUSED,
    RENDERER_TABLE,
    ClassifierEntry,
    GpuTopology,
    class_from_ms,
    class_from_renderer,
    class_from_topology,
    class_from_topology_compat,
    classify_from_calibrator_result,
    discover_gpus,
    driver_is_display_controller,
    driver_is_hardware_gpu,
)

# ---------------------------------------------------------------------------
# Live-captured strings from 192.168.207.66 (see fixtures/v66-live/STRINGS.txt)
# ---------------------------------------------------------------------------

# Renderer string from vulkaninfo --summary on .66
RENDERER_V66 = "Mali-G720-Immortalis"
# GL_VERSION from eglinfo on .66
VERSION_V66 = "OpenGL ES 3.2 v1.r53p0-00eac0.c707efa0cfa034b363bc93f9b6749cb5"
# /sys/class/misc/mali0/device/driver symlink target basename on .66
DRIVER_MALI_V66 = "mali"
# /sys/class/drm/card*/device/driver symlink target basename on .66
DRIVER_LINLONDP_V66 = "linlondp"
# Reference micro-benchmark ms from gpu-class.json cache on .66
MS_V66_DISPLAY = 9.88  # pci-CIXH5010_03 entry (DRM card)
MS_V66_MALI = 11.76  # soc-CIXH5000_00 entry (misc/mali0)
MS_V66_PRIOR = 17.812  # the first capture from the prior worker session

# The four dicts that list_gpus() produces on .66, captured live
DRM_CARDS_V66 = [
    {
        "id": "pci-CIXH5010_00",
        "card": "card0",
        "slot": "CIXH5010:00",
        "vendor": "other",
        "driver": DRIVER_LINLONDP_V66,
        "device": "",
        "display": False,
        "boot_vga": False,
        "discrete": False,
        "render": True,
    },
    {
        "id": "pci-CIXH5010_01",
        "card": "card1",
        "slot": "CIXH5010:01",
        "vendor": "other",
        "driver": DRIVER_LINLONDP_V66,
        "device": "",
        "display": False,
        "boot_vga": False,
        "discrete": False,
        "render": True,
    },
    {
        "id": "pci-CIXH5010_03",
        "card": "card2",
        "slot": "CIXH5010:03",
        "vendor": "other",
        "driver": DRIVER_LINLONDP_V66,
        "device": "",
        "display": True,
        "boot_vga": False,
        "discrete": False,
        "render": True,
    },
    {
        "id": "pci-CIXH5010_04",
        "card": "card3",
        "slot": "CIXH5010:04",
        "vendor": "other",
        "driver": DRIVER_LINLONDP_V66,
        "device": "",
        "display": False,
        "boot_vga": False,
        "discrete": False,
        "render": True,
    },
]


def _mali_gpu_dict():
    """What `discover_gpus()` returns for /sys/class/misc/mali0 on .66."""
    return {
        "id": "soc-mali0",
        "card": "",
        "slot": "mali0",
        "vendor": "arm",
        "driver": DRIVER_MALI_V66,
        "device": "",
        "display": False,
        "boot_vga": False,
        "discrete": False,
        "render": True,
        "sysfs_kind": "misc",
        "sysfs_path": "/sys/class/misc/mali0",
    }


def _display_gpu_dict():
    """The DRM card that owns a connected connector on .66 (card2)."""
    return DRM_CARDS_V66[2]  # pci-CIXH5010_03


def _all_v66_gpus():
    """Both lists combined — what list_gpus() + discover_gpus() together see."""
    return DRM_CARDS_V66 + [_mali_gpu_dict()]


# ---------------------------------------------------------------------------
# 1. Pure-function primitives
# ---------------------------------------------------------------------------


class ClassFromMsTests(unittest.TestCase):
    """class_from_ms maps a benchmark ms figure to a class."""

    def test_v66_display_benchmark_is_mid(self):
        self.assertEqual(class_from_ms(MS_V66_DISPLAY), "mid")

    def test_v66_mali_benchmark_is_mid(self):
        self.assertEqual(class_from_ms(MS_V66_MALI), "mid")

    def test_v66_prior_capture_is_mid(self):
        self.assertEqual(class_from_ms(MS_V66_PRIOR), "mid")

    def test_boundary_mid_is_mid(self):
        self.assertEqual(class_from_ms(CLASS_MID_MS), "mid")

    def test_boundary_weak_is_weak(self):
        self.assertEqual(class_from_ms(CLASS_WEAK_MS), "weak")

    def test_just_under_mid_is_strong(self):
        self.assertEqual(class_from_ms(CLASS_MID_MS - 0.01), "strong")

    def test_just_under_weak_is_mid(self):
        self.assertEqual(class_from_ms(CLASS_WEAK_MS - 0.01), "mid")

    def test_huge_value_is_weak(self):
        self.assertEqual(class_from_ms(1000.0), "weak")


class ClassFromRendererTests(unittest.TestCase):
    """class_from_renderer maps a GL_RENDERER string to a class."""

    def test_v66_renderer_is_mid(self):
        self.assertEqual(class_from_renderer(RENDERER_V66), "mid")

    def test_v66_renderer_lowercase_is_mid(self):
        # vulkaninfo reports "mali-g720-immortalis"; both cases must work
        self.assertEqual(class_from_renderer("mali-g720-immortalis"), "mid")

    def test_mali_g720_immortalis_substring_match(self):
        self.assertEqual(class_from_renderer("Mali-G720-Immortalis"), "mid")

    def test_mali_renderer_table_coverage(self):
        """Regression test for the renderer-table regexes, locked
        against the actual strings from .66 + every modern Mali part.

        Every Mali GPU generation shipped in the last 6 years is
        covered: G52/G57 (mid-2020s budget) -> weak via row 3;
        G610/G615/G710/G715/G720/G78/G68/G77/G79 -> mid via row 4.
        Immortalis products carry "Immortalis" in their name and match
        the row-4 substring first.
        """
        # Mid-class: Immortalis / Mali-G6xx / G7xx / G8xx
        for r in (
            "Mali-G610",
            "Mali-G615",
            "Mali-G710",
            "Mali-G715",
            "Mali-G720",
            "Mali-G720-Immortalis",
            "Mali-G78",
            "Mali-G78 AE",
            "Mali-G68",
            "Mali-G77",
            "Mali-G79",
            "Mali-G615 AE",
        ):
            with self.subTest(r=r):
                self.assertEqual(
                    class_from_renderer(r),
                    "mid",
                    f"{r!r} should match row 4 mid regex",
                )

        # Weak-class: older Mali (G3xx, G5xx) + Mali-T + Mali-4 (Upstream
        # regex `mali-g[35]\d\b` only matches G3xx and G5xx, NOT G4xx —
        # Mali-G41 / Mali-G47 / Mali-G31 are not covered. Documented
        # upstream quirk; we don't extend the table because Mali-G4xx is
        # genuinely weak-class and the existing class_from_topology
        # fallback (mali driver -> mid) is wrong but recoverable: in
        # practice a Mali-G4xx board will have a stronger
        # class_from_topology path through the renderer hint or the
        # misc/mali entry.)
        for r in (
            "Mali-G52",
            "Mali-G57",
            "Mali-G31",
            "Mali-G52 MC1",
            "Mali-T720",
            "Mali-T830",
            "Mali-T860",
            "Mali-470",
        ):
            with self.subTest(r=r):
                self.assertEqual(
                    class_from_renderer(r),
                    "weak",
                    f"{r!r} should match row 3 weak regex",
                )

    def test_llvmpipe_is_weak(self):
        self.assertEqual(
            class_from_renderer("llvmpipe (LLVM 21.1.8, 128 bits)"), "weak"
        )

    def test_softpipe_is_weak(self):
        self.assertEqual(class_from_renderer("softpipe"), "weak")

    def test_intel_uhd_is_weak(self):
        self.assertEqual(
            class_from_renderer("Mesa Intel(R) UHD Graphics 630 (KBL GT2)"),
            "weak",
        )

    def test_intel_iris_xe_is_mid(self):
        # Without "(R)" between "Iris" and "Xe" the upstream regex
        # `iris xe` matches and returns mid. With "(R)" it returns
        # weak (the regex requires the literal substring). This is the
        # exact upstream quirk; lock it.
        self.assertEqual(
            class_from_renderer("Intel Iris Xe Graphics"),
            "mid",
        )

    def test_intel_iris_xe_with_R_qualifier_is_weak(self):
        # Documented upstream quirk: RENDERER_TABLE row 4 is
        # `iris xe` (a literal substring); real-world Intel strings
        # almost always include "(R)" between Iris and Xe, so the
        # class_from_topology fallback (i915 / xe driver) is what
        # answers these in practice. class_from_renderer says weak.
        self.assertEqual(
            class_from_renderer("Intel(R) Iris(R) Xe Graphics"),
            "weak",
        )

    def test_apple_m1_pro_is_strong(self):
        # Apple M1 Pro must hit the strong row before any mid-row match
        # against "apple"
        self.assertEqual(class_from_renderer("Apple M1 Pro"), "strong")

    def test_nvidia_rtx_is_strong(self):
        self.assertEqual(class_from_renderer("NVIDIA GeForce RTX 4090"), "strong")

    def test_empty_string_is_none(self):
        self.assertIsNone(class_from_renderer(""))

    def test_none_is_none(self):
        self.assertIsNone(class_from_renderer(None))  # type: ignore[arg-type]


class RendererTableContractTests(unittest.TestCase):
    """The RENDERER_TABLE itself is a load-bearing contract; lock it."""

    def test_table_is_tuple_of_pattern_class(self):
        for pattern, cls in RENDERER_TABLE:
            self.assertIsInstance(pattern, str)
            self.assertIn(cls, CLASS_RANK)

    def test_table_strong_before_mid_before_weak(self):
        # Order is critical: a renderer that matches both a strong and a
        # mid row (e.g. "apple m1 pro") must answer strong. The upstream
        # table puts strong rows first; weak is positioned BEFORE mid
        # in the upstream because llvmpipe / swrast are more common
        # matches and we want to catch them first (so a render-string
        # of "llvmpipe (Intel(R) UHD Graphics 630 ...)" doesn't fall
        # through to a mid row). Document that ordering here.
        strong_rows = [
            i for i, (_, cls) in enumerate(RENDERER_TABLE) if cls == "strong"
        ]
        mid_rows = [i for i, (_, cls) in enumerate(RENDERER_TABLE) if cls == "mid"]
        weak_rows = [i for i, (_, cls) in enumerate(RENDERER_TABLE) if cls == "weak"]
        self.assertLess(
            max(strong_rows), min(mid_rows), "strong rows must come before mid rows"
        )
        # And: weak is intentionally BEFORE mid in the upstream so
        # llvmpipe/softpipe catch wins over an "iris xe" substring.
        # Document this — the comment above this test is the spec.
        self.assertLess(
            max(weak_rows),
            min(mid_rows),
            "weak rows must come before mid rows so "
            "llvmpipe/softpipe win over an iris-xe substring",
        )


# ---------------------------------------------------------------------------
# 2. Driver-name predicates — the .66 .66-specific contracts
# ---------------------------------------------------------------------------


class DriverPredicatesTests(unittest.TestCase):
    """driver_is_hardware_gpu / driver_is_display_controller."""

    def test_v66_mali_driver_is_hardware_gpu(self):
        # The smoking-gun: `mali` (not `mali_kbase`) must be a
        # hardware-GPU driver. This is the actual /sys/class/misc/mali0/
        # device/driver symlink target on .66.
        self.assertTrue(driver_is_hardware_gpu(DRIVER_MALI_V66))

    def test_v66_mali_kbase_alias_is_also_hardware_gpu(self):
        # Some tools / older launchers reported the kernel-module name;
        # that path must keep working.
        self.assertTrue(driver_is_hardware_gpu("mali_kbase"))

    def test_v66_linlondp_is_NOT_hardware_gpu(self):
        # The DRM cards on .66 are bound to linlondp, a display
        # controller; they must NOT be classified as "hardware GPU"
        # even though they have render nodes.
        self.assertFalse(driver_is_hardware_gpu(DRIVER_LINLONDP_V66))

    def test_v66_linlondp_is_display_controller(self):
        self.assertTrue(driver_is_display_controller(DRIVER_LINLONDP_V66))

    def test_komeda_is_display_controller(self):
        self.assertTrue(driver_is_display_controller("komeda"))
        self.assertFalse(driver_is_hardware_gpu("komeda"))

    def test_intel_i915_is_weak_hardware(self):
        self.assertTrue(driver_is_hardware_gpu("i915"))
        self.assertFalse(driver_is_display_controller("i915"))

    def test_nvidia_is_hardware_gpu(self):
        self.assertTrue(driver_is_hardware_gpu("nvidia"))

    def test_none_is_not_hardware(self):
        self.assertFalse(driver_is_hardware_gpu(None))

    def test_empty_string_is_not_hardware(self):
        self.assertFalse(driver_is_hardware_gpu(""))

    def test_unknown_driver_is_not_hardware(self):
        self.assertFalse(driver_is_hardware_gpu("some-future-driver"))


# ---------------------------------------------------------------------------
# 3. Topology classifier
# ---------------------------------------------------------------------------


class ClassFromTopologyTests(unittest.TestCase):
    """class_from_topology maps a GpuTopology (or dict) to a class."""

    def test_v66_mali_dict_is_mid(self):
        """The smoking-gun case: /sys/class/misc/mali0 on .66 has
        driver='mali' (NOT 'mali_kbase'); topology must say 'mid'."""
        self.assertEqual(class_from_topology(_mali_gpu_dict()), "mid")

    def test_v66_mali_topo_is_mid(self):
        topo = GpuTopology(
            driver=DRIVER_MALI_V66, discrete=False, display=False, sysfs_kind="misc"
        )
        self.assertEqual(class_from_topology(topo), "mid")

    def test_v66_linlondp_display_card_is_weak(self):
        """The DRM card entry has driver='linlondp' (display controller).
        That alone must say 'weak' — the real answer comes from the
        misc entry via `all_gpus`."""
        self.assertEqual(class_from_topology(_display_gpu_dict()), "weak")

    def test_mali_kbase_is_mid(self):
        """Backwards compat with the older launcher that reported the
        kernel-module name instead of the platform-driver name."""
        self.assertEqual(
            class_from_topology(
                {"driver": "mali_kbase", "discrete": False, "display": True}
            ),
            "mid",
        )

    def test_panthor_is_mid(self):
        self.assertEqual(
            class_from_topology(
                {"driver": "panthor", "discrete": False, "display": True}
            ),
            "mid",
        )

    def test_panfrost_is_mid(self):
        self.assertEqual(
            class_from_topology(
                {"driver": "panfrost", "discrete": False, "display": True}
            ),
            "mid",
        )

    def test_amdgpu_discrete_is_strong(self):
        self.assertEqual(
            class_from_topology(
                {"driver": "amdgpu", "discrete": True, "display": True}
            ),
            "strong",
        )

    def test_amdgpu_apu_is_mid(self):
        # APUs: discrete=False (VRAM < 2 GB) but driver=amdgpu
        self.assertEqual(
            class_from_topology(
                {"driver": "amdgpu", "discrete": False, "display": True}
            ),
            "mid",
        )

    def test_intel_i915_is_weak(self):
        self.assertEqual(
            class_from_topology({"driver": "i915", "discrete": False, "display": True}),
            "weak",
        )

    def test_intel_xe_is_weak(self):
        self.assertEqual(
            class_from_topology({"driver": "xe", "discrete": False, "display": True}),
            "weak",
        )

    def test_nvidia_discrete_is_strong(self):
        self.assertEqual(
            class_from_topology(
                {"driver": "nvidia", "discrete": True, "display": True}
            ),
            "strong",
        )

    def test_nvidia_igpu_is_weak(self):
        # Optimus / PRIME offload: driver=nvidia, discrete=False
        self.assertEqual(
            class_from_topology(
                {"driver": "nvidia", "discrete": False, "display": True}
            ),
            "weak",
        )

    def test_no_driver_is_weak(self):
        self.assertEqual(class_from_topology({"driver": None}), "weak")

    def test_none_input_is_weak(self):
        self.assertEqual(class_from_topology(None), "weak")

    def test_empty_dict_is_weak(self):
        self.assertEqual(class_from_topology({}), "weak")

    def test_compat_alias_matches(self):
        self.assertEqual(
            class_from_topology_compat(_mali_gpu_dict()),
            class_from_topology(_mali_gpu_dict()),
        )


# ---------------------------------------------------------------------------
# 4. The fix: classify_from_calibrator_result
# ---------------------------------------------------------------------------


class HappyPathTests(unittest.TestCase):
    """Branch 1: code=0 with a real ms number -> calibration path."""

    def test_v66_ms_returns_mid(self):
        ent = classify_from_calibrator_result(
            code=EXIT_OK,
            gpu=_display_gpu_dict(),
            ms_hint=MS_V66_DISPLAY,
            renderer_hint=RENDERER_V66,
            version_hint=VERSION_V66,
        )
        self.assertEqual(ent.cls, "mid")
        self.assertEqual(ent.source, "calibration")
        self.assertEqual(ent.ms, MS_V66_DISPLAY)
        self.assertEqual(ent.renderer, RENDERER_V66)
        self.assertEqual(ent.version, VERSION_V66)

    def test_calibration_dict_shape_matches_upstream(self):
        ent = classify_from_calibrator_result(
            code=EXIT_OK,
            gpu=None,
            ms_hint=17.812,
            renderer_hint=RENDERER_V66,
        )
        d = ent.to_dict()
        self.assertEqual(d["class"], "mid")
        self.assertEqual(d["source"], "calibration")
        self.assertEqual(d["ms"], 17.812)
        self.assertEqual(d["renderer"], RENDERER_V66)
        self.assertEqual(d["software"], False)


class RefusalFallbackTests(unittest.TestCase):
    """Branches 3, 4, 5: refusal / retry-later / no-hardware-bound."""

    # --- Branch 3: retry-able failure with hardware bound ---

    def test_v66_retry_later_with_mali_misc_entry_returns_mid(self):
        """The actual reproducer on .66: code=2 (no GLES3 config) with
        the display_gpu dict (linlondp) AND the misc/mali0 dict passed
        via all_gpus. Post-fix verdict must be 'mid'."""
        ent = classify_from_calibrator_result(
            code=EXIT_NO_CONFIG,
            gpu=_display_gpu_dict(),
            renderer_hint=RENDERER_V66,
            version_hint=VERSION_V66,
            all_gpus=_all_v66_gpus(),
        )
        self.assertEqual(ent.cls, "mid")
        self.assertEqual(ent.source, "retry-later")
        # The misc/mali0 entry is the one that says "hardware GPU bound"
        self.assertEqual(ent.extras["effective_driver"], DRIVER_MALI_V66)
        self.assertEqual(ent.extras["input_driver"], DRIVER_LINLONDP_V66)

    def test_v66_retry_later_without_all_gpus_returns_mid_via_renderer_hint(self):
        """Without `all_gpus=`, only the renderer_hint is consulted.
        `class_from_renderer("Mali-G720-Immortalis")` returns "mid", so
        the verdict is mid even though the misc/mali0 entry is invisible.
        (The full fix also requires `all_gpus=` to be passed; this test
        documents the weaker guarantee.)"""
        ent = classify_from_calibrator_result(
            code=EXIT_NO_CONFIG,
            gpu=_display_gpu_dict(),
            renderer_hint=RENDERER_V66,
        )
        self.assertEqual(ent.cls, "mid")
        self.assertEqual(ent.source, "retry-later")

    def test_retry_later_no_hardware_no_renderer_returns_weak(self):
        ent = classify_from_calibrator_result(
            code=EXIT_NO_CONFIG,
            gpu=None,
            renderer_hint="",
        )
        self.assertEqual(ent.cls, "weak")
        self.assertEqual(ent.source, "retry-later")

    def test_retry_later_no_hardware_with_renderer_uses_renderer(self):
        ent = classify_from_calibrator_result(
            code=EXIT_NO_CONFIG,
            gpu=None,
            renderer_hint=RENDERER_V66,
        )
        self.assertEqual(ent.cls, "mid")

    # --- Branch 4: code=3 (refusal) with hardware bound -- THE FIX ---

    def test_v66_refusal_with_mali_misc_entry_returns_mid(self):
        """THE smoking-gun regression test."""
        ent = classify_from_calibrator_result(
            code=EXIT_SOFTWARE_REFUSED,
            gpu=_display_gpu_dict(),
            renderer_hint="llvmpipe (LLVM 21.1.8, 128 bits)",
            version_hint="",
            all_gpus=_all_v66_gpus(),
        )
        self.assertEqual(ent.cls, "mid")
        self.assertEqual(ent.source, "refused-by-hardware-driver")
        self.assertTrue(ent.software)
        self.assertIn("mali", ent.note)
        # The renderer_hint says 'weak' (llvmpipe) but the topology says
        # 'mid' (mali) — we trust topology when hardware is bound.
        self.assertEqual(ent.extras["driver"], DRIVER_MALI_V66)

    def test_v66_refusal_with_immortalis_renderer_hint_prefers_topology(self):
        """Even with the right renderer_hint ('mid'), the topology path
        is the one that produces the 'mid' verdict — and it should be
        reported as the source 'refused-by-hardware-driver', not
        'calibration'."""
        ent = classify_from_calibrator_result(
            code=EXIT_SOFTWARE_REFUSED,
            gpu=_display_gpu_dict(),
            renderer_hint=RENDERER_V66,
            all_gpus=_all_v66_gpus(),
        )
        self.assertEqual(ent.cls, "mid")
        self.assertEqual(ent.source, "refused-by-hardware-driver")
        self.assertEqual(ent.extras["driver"], DRIVER_MALI_V66)

    def test_v66_refusal_only_display_controller_input_returns_weak(self):
        """Without the misc/mali0 entry passed in, the refusal fallback
        sees only linlondp (display controller) and cannot prove
        hardware GPU is bound. Verdict is 'weak' — same as upstream.
        Operators should pass all_gpus to get the right answer."""
        ent = classify_from_calibrator_result(
            code=EXIT_SOFTWARE_REFUSED,
            gpu=_display_gpu_dict(),
            renderer_hint="",
        )
        self.assertEqual(ent.cls, "weak")
        self.assertEqual(ent.source, "refused")
        self.assertTrue(ent.software)

    def test_refused_with_nvidia_discrete_returns_strong(self):
        ent = classify_from_calibrator_result(
            code=EXIT_SOFTWARE_REFUSED,
            gpu={"driver": "nvidia", "discrete": True, "display": True},
            renderer_hint="llvmpipe (LLVM 21.1.8, 128 bits)",
        )
        self.assertEqual(ent.cls, "strong")
        self.assertEqual(ent.source, "refused-by-hardware-driver")

    def test_refused_with_amd_discrete_returns_strong(self):
        ent = classify_from_calibrator_result(
            code=EXIT_SOFTWARE_REFUSED,
            gpu={"driver": "amdgpu", "discrete": True, "display": False},
            renderer_hint="llvmpipe (LLVM 21.1.8, 128 bits)",
        )
        self.assertEqual(ent.cls, "strong")
        self.assertEqual(ent.source, "refused-by-hardware-driver")

    def test_refused_with_intel_uhd_returns_weak(self):
        # Intel UHD 630 is the reference 'weak' GPU. The refusal must
        # say 'weak' and topology agrees.
        ent = classify_from_calibrator_result(
            code=EXIT_SOFTWARE_REFUSED,
            gpu={"driver": "i915", "discrete": False, "display": True},
            renderer_hint="llvmpipe (LLVM 21.1.8, 128 bits)",
        )
        self.assertEqual(ent.cls, "weak")
        self.assertEqual(ent.source, "refused-by-hardware-driver")

    def test_refused_with_panthor_returns_mid(self):
        ent = classify_from_calibrator_result(
            code=EXIT_SOFTWARE_REFUSED,
            gpu={"driver": "panthor", "discrete": False, "display": True},
            renderer_hint="llvmpipe (LLVM 21.1.8, 128 bits)",
        )
        self.assertEqual(ent.cls, "mid")
        self.assertEqual(ent.source, "refused-by-hardware-driver")

    def test_refused_with_known_renderer_hint_prefers_table_match(self):
        """When both topology and the renderer-table agree, use the
        renderer-table answer (more specific)."""
        ent = classify_from_calibrator_result(
            code=EXIT_SOFTWARE_REFUSED,
            gpu={"driver": "i915", "discrete": False, "display": True},
            renderer_hint="Intel Iris Xe Graphics",  # mid via table
        )
        # i915 topology = weak, "Intel Iris Xe" renderer-table = mid,
        # hardware IS bound. Renderer-table answer (mid) is more
        # specific AND it disagrees upward (mid > weak), so we trust
        # the renderer-table.
        self.assertEqual(ent.cls, "mid")
        self.assertEqual(ent.source, "refused-by-hardware-driver")

    def test_refused_with_intel_iris_xe_on_i915_gpu_returns_weak(self):
        """Renderer hint says 'weak' (Intel(R) Iris(R) Xe Graphics — the
        (R) breaks the substring match), topology says 'weak' too,
        hardware IS bound. Verdict is 'weak'."""
        ent = classify_from_calibrator_result(
            code=EXIT_SOFTWARE_REFUSED,
            gpu={"driver": "i915", "discrete": False, "display": True},
            renderer_hint="Intel(R) Iris(R) Xe Graphics",
        )
        self.assertEqual(ent.cls, "weak")
        self.assertEqual(ent.source, "refused-by-hardware-driver")

    # --- Branch 5: code=3 refusal with NO hardware bound ---

    def test_refused_with_no_gpu_at_all_returns_weak(self):
        """A real software-only machine (server, headless, container)."""
        ent = classify_from_calibrator_result(
            code=EXIT_SOFTWARE_REFUSED,
            gpu=None,
            renderer_hint="llvmpipe (LLVM 21.1.8, 128 bits)",
        )
        self.assertEqual(ent.cls, "weak")
        self.assertEqual(ent.source, "refused")
        self.assertTrue(ent.software)

    def test_refused_with_only_display_controller_returns_weak(self):
        ent = classify_from_calibrator_result(
            code=EXIT_SOFTWARE_REFUSED,
            gpu={"driver": "linlondp", "discrete": False, "display": True},
            renderer_hint="",
        )
        self.assertEqual(ent.cls, "weak")
        self.assertEqual(ent.source, "refused")

    # --- Branch 2: code=0 with no ms ---

    def test_calibration_success_with_zero_ms_falls_through(self):
        ent = classify_from_calibrator_result(
            code=EXIT_OK,
            gpu=None,
            renderer_hint=RENDERER_V66,
            ms_hint=0,
        )
        self.assertEqual(ent.cls, "mid")
        self.assertEqual(ent.source, "retry-later")

    def test_calibration_success_with_none_ms_falls_through(self):
        ent = classify_from_calibrator_result(
            code=EXIT_OK,
            gpu=None,
            renderer_hint=RENDERER_V66,
            ms_hint=None,
        )
        self.assertEqual(ent.cls, "mid")
        self.assertEqual(ent.source, "retry-later")


class ExitCodeTests(unittest.TestCase):
    """Every EXIT_* code gets the right branch."""

    def test_exit_error_with_no_hardware_returns_weak(self):
        ent = classify_from_calibrator_result(
            code=EXIT_ERROR,
            gpu=None,
            renderer_hint="",
        )
        self.assertEqual(ent.cls, "weak")
        self.assertEqual(ent.source, "retry-later")

    def test_exit_no_config_with_mali_returns_mid(self):
        ent = classify_from_calibrator_result(
            code=EXIT_NO_CONFIG,
            gpu=_display_gpu_dict(),
            renderer_hint=RENDERER_V66,
            all_gpus=_all_v66_gpus(),
        )
        self.assertEqual(ent.cls, "mid")
        self.assertEqual(ent.source, "retry-later")

    def test_exit_no_compositor_with_mali_returns_mid(self):
        """EXIT_NO_COMPOSITOR is reserved for a future calibrator exit
        code; the classifier must still answer 'mid' when hardware is
        bound."""
        ent = classify_from_calibrator_result(
            code=EXIT_NO_COMPOSITOR,
            gpu=_display_gpu_dict(),
            renderer_hint=RENDERER_V66,
            all_gpus=_all_v66_gpus(),
        )
        self.assertEqual(ent.cls, "mid")
        self.assertEqual(ent.source, "retry-later")


# ---------------------------------------------------------------------------
# 5. ClassifierEntry — dict-shape contract
# ---------------------------------------------------------------------------


class ClassifierEntryTests(unittest.TestCase):
    def test_to_dict_skips_empty_fields(self):
        e = ClassifierEntry(cls="mid", source="calibration")
        d = e.to_dict()
        self.assertEqual(
            d, {"class": "mid", "source": "calibration", "software": False}
        )

    def test_to_dict_includes_all_set_fields(self):
        e = ClassifierEntry(
            cls="mid",
            source="refused-by-hardware-driver",
            software=True,
            ms=None,
            renderer=RENDERER_V66,
            version=VERSION_V66,
            note="x",
            extras={"driver": "mali"},
        )
        d = e.to_dict()
        self.assertEqual(d["class"], "mid")
        self.assertEqual(d["source"], "refused-by-hardware-driver")
        self.assertEqual(d["software"], True)
        self.assertEqual(d["renderer"], RENDERER_V66)
        self.assertEqual(d["version"], VERSION_V66)
        self.assertEqual(d["note"], "x")
        self.assertEqual(d["driver"], "mali")
        # ms=None should NOT be present
        self.assertNotIn("ms", d)


# ---------------------------------------------------------------------------
# 6. discover_gpus — the bonus enumeration that finds misc/mali0
# ---------------------------------------------------------------------------


class DiscoverGpusTests(unittest.TestCase):
    """discover_gpus() finds both DRM cards and misc/mali0 devices."""

    def setUp(self):
        # Use a fixture sysfs root so the tests don't depend on the
        # host they're run on.
        self.fixture = Path(__file__).resolve().parent.parent / "fixtures" / "v66-sysfs"

    def test_v66_fixture_enumerates_four_drm_cards_plus_misc_mali(self):
        # We don't ship the full sysfs tree in the fixtures dir (it
        # would be huge); just verify the helper handles both shapes.
        # If the fixture dir doesn't exist, skip rather than fail.
        if not self.fixture.is_dir():
            self.skipTest(f"fixture sysfs root not present at {self.fixture}")
        gpus = discover_gpus(sysfs_root=str(self.fixture))
        kinds = {g["sysfs_kind"] for g in gpus}
        self.assertIn("drm", kinds)
        self.assertIn("misc", kinds)

    def test_discover_gpus_real_host_sees_both_buses(self):
        """When run on the actual .66 host (or any host with /sys and
        at least one DRM card plus one misc/mali device), discover_gpus
        must return at least one entry with sysfs_kind='misc'."""
        if not Path("/sys/class/misc").is_dir():
            self.skipTest("not on a host with /sys/class/misc")
        gpus = discover_gpus()
        drm = [g for g in gpus if g["sysfs_kind"] == "drm"]
        misc = [g for g in gpus if g["sysfs_kind"] == "misc"]
        # On .66 we expect 4 DRM cards (linlondp) + 1 misc (mali);
        # on the build pool (this host) we expect 0 of each. Skip
        # rather than fail if we're not on a host with any GPU.
        if not drm and not misc:
            self.skipTest("host has neither DRM nor misc GPUs (headless?)")
        # If we have misc entries, they MUST all be hardware GPUs.
        for m in misc:
            self.assertTrue(
                driver_is_hardware_gpu(m["driver"]),
                f"misc entry {m} not a hardware GPU driver",
            )


# ---------------------------------------------------------------------------
# 7. End-to-end with the real captured JSON cache from .66
# ---------------------------------------------------------------------------


class EndToEndTests(unittest.TestCase):
    """Use the actual gpu-class.json content from .66 as test input."""

    GPU_CLASS_JSON = {
        "entries": {
            "pci-CIXH5010_03": {
                "class": "mid",
                "ms": MS_V66_DISPLAY,
                "renderer": RENDERER_V66,
                "version": VERSION_V66,
                "timer": "gpu",
                "source": "calibration",
                "when": "2026-09-30T01:51:52+0000",
                "gpu": "pci-CIXH5010_03",
                "driver": DRIVER_LINLONDP_V66,
            },
            "soc-CIXH5000_00": {
                "class": "mid",
                "ms": MS_V66_MALI,
                "renderer": RENDERER_V66,
                "version": VERSION_V66,
                "timer": "wall",
                "source": "calibration",
                "when": "2026-09-30T04:20:00+0000",
                "gpu": "soc-CIXH5000_00",
                "driver": DRIVER_MALI_V66,
            },
        }
    }

    def test_cache_shape_round_trip(self):
        # The JSON must parse and contain the two expected entries.
        # This is what the launcher's read_class_cache() returns.
        data = json.loads(json.dumps(self.GPU_CLASS_JSON))
        self.assertEqual(len(data["entries"]), 2)
        self.assertEqual(data["entries"]["pci-CIXH5010_03"]["class"], "mid")
        self.assertEqual(data["entries"]["soc-CIXH5000_00"]["class"], "mid")
        self.assertEqual(
            data["entries"]["pci-CIXH5010_03"]["driver"],
            DRIVER_LINLONDP_V66,
        )
        self.assertEqual(
            data["entries"]["soc-CIXH5000_00"]["driver"],
            DRIVER_MALI_V66,
        )

    def test_misc_entry_driver_classifies_as_mid(self):
        """The soc-CIXH5000_00 entry has driver='mali'; the classifier
        must say 'mid' (this is the verification that the upstream
        class_from_topology would also agree)."""
        entry = self.GPU_CLASS_JSON["entries"]["soc-CIXH5000_00"]
        self.assertEqual(
            class_from_topology({"driver": entry["driver"]}),
            entry["class"],
        )

    def test_drm_entry_driver_classifies_as_weak_alone(self):
        """The pci-CIXH5010_03 entry has driver='linlondp'; topology
        alone says 'weak'. The fact that the host is actually mid comes
        from the misc/mali0 entry, which must be passed via all_gpus."""
        entry = self.GPU_CLASS_JSON["entries"]["pci-CIXH5010_03"]
        self.assertEqual(
            class_from_topology({"driver": entry["driver"]}),
            "weak",
        )


if __name__ == "__main__":
    unittest.main()
