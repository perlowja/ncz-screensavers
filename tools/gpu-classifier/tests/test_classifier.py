# test_classifier.py — unit tests for tools/gpu-classifier/.
#
# Standard library only (unittest). No mocks, no subprocess, no fixtures.
# The constants below are real values captured from the live NCZ-OS
# 26.7 Maximilian host 192.168.207.66 (cixmini, Sky1, Mali-G720-Immortalis)
# on 2026-09-30 02:45 UTC. See README.md and EVIDENCE.md for the literal
# command transcripts that produced them.
#
# Run from anywhere with:
#     python3 -m unittest discover -v tools/gpu-classifier/tests
#
# Or from the package directory:
#     python3 -m unittest discover -v tests

from __future__ import annotations

import os
import sys
import unittest

# Make the gpu-classifier package importable when the test is run as a
# script from any cwd, not just from inside the package.
_HERE = os.path.dirname(os.path.abspath(__file__))
_PKG_PARENT = os.path.dirname(_HERE)
if _PKG_PARENT not in sys.path:
    sys.path.insert(0, _PKG_PARENT)

from gpu_classifier import (  # noqa: E402
    CLASS_MID_MS,
    CLASS_WEAK_MS,
    EXIT_ERROR,
    EXIT_NO_CONFIG,
    EXIT_NO_COMPOSITOR,
    EXIT_OK,
    EXIT_SOFTWARE_REFUSED,
    ClassifierEntry,
    GpuTopology,
    RENDERER_TABLE,
    classify_from_calibrator_result,
    class_from_ms,
    class_from_renderer,
    class_from_topology,
    class_from_topology_compat,
)


# ---------------------------------------------------------------------------
# Real values captured from .66 on 2026-09-30.
#
# These constants are the proof that the bug exists. They are also the
# proof that the fix works: a unit test that runs classify_from_calibrator
# against these exact values and asserts the right answer is, by
# construction, the same check the live host will run.
# ---------------------------------------------------------------------------

# From vulkaninfo on .66:
MALI_DEVICE_NAME_V66 = "Mali-G720-Immortalis"
MALI_VENDOR_ID_V66 = "0x13b5"  # ARM
MALI_API_VERSION_V66 = "1.3.296"  # 4206888
MALI_DRIVER_VERSION_V66 = "53.0.0"  # r53p0 in the GL_VERSION string

# From eglinfo on .66:
MALI_EGL_VERSION_V66 = "OpenGL ES 3.2 v1.r53p0-00eac0.c707efa0cfa034b363bc93f9b6749cb5"

# The full --identify output the calibrator prints on .66 when given
# the right LD_LIBRARY_PATH:
MALI_IDENTIFY_JSON_V66 = (
    '{"renderer":"Mali-G720-Immortalis",'
    '"version":"OpenGL ES 3.2 v1.r53p0-00eac0.c707efa0cfa034b363bc93f9b6749cb5",'
    '"platform":"default","identify":true}'
)

# The full benchmark output the calibrator prints on .66:
MALI_BENCHMARK_JSON_V66 = (
    '{"renderer":"Mali-G720-Immortalis",'
    '"version":"OpenGL ES 3.2 v1.r53p0-00eac0.c707efa0cfa034b363bc93f9b6749cb5",'
    '"platform":"default","timer":"wall","frames":31,"ms":17.812}'
)

# From /sys/class/drm/card*/device/driver on .66 (all four DRM cards
# are bound to linlondp, the display controller; the renderer is the
# separate /dev/mali0 with mali_kbase):
DRIVER_BOUND_V66 = "mali_kbase"  # /sys/class/misc/mali0/device/driver

# The Mesa llvmpipe renderer string that the calibrator sees when it
# runs WITHOUT the cixgpu-pro loader path (this is what bit /usr/bin/
# ncz-screensaver on .66 before the fix):
LLVMIPE_RENDERER_V66 = "llvmpipe (LLVM 21.1.8, 128 bits)"


# ---------------------------------------------------------------------------
# class_from_ms — boundary table from the launcher's constant table
# ---------------------------------------------------------------------------


class ClassFromMsTests(unittest.TestCase):
    """class_from_ms must match the launcher's exact thresholds."""

    def test_weak_at_threshold(self):
        # At-or-above 20.0 ms is "weak".
        self.assertEqual(class_from_ms(20.0), "weak")
        self.assertEqual(class_from_ms(20.001), "weak")
        self.assertEqual(class_from_ms(60.0), "weak")

    def test_mid_at_threshold(self):
        # Below 20.0 AND at-or-above 8.0 is "mid".
        self.assertEqual(class_from_ms(19.999), "mid")
        self.assertEqual(class_from_ms(CLASS_WEAK_MS - 0.001), "mid")
        self.assertEqual(class_from_ms(8.0), "mid")

    def test_strong_below_threshold(self):
        # Below 8.0 ms is "strong" (e.g. an RTX 4060 fillrate benchmark).
        self.assertEqual(class_from_ms(7.999), "strong")
        self.assertEqual(class_from_ms(CLASS_MID_MS - 0.001), "strong")
        self.assertEqual(class_from_ms(0.5), "strong")

    def test_mali_g720_v66_lives_in_mid_band(self):
        """The actual benchmark on .66 returned ms=17.812.

        This must classify as 'mid'. If class_from_ms is ever retuned
        such that 17.812 falls into 'weak', this test will fail and
        the fix will be obviously broken on Mali-G720-Immortalis.
        """
        self.assertEqual(class_from_ms(17.812), "mid")


# ---------------------------------------------------------------------------
# class_from_renderer — the strings we will actually see
# ---------------------------------------------------------------------------


class ClassFromRendererTests(unittest.TestCase):
    """class_from_renderer must agree with RENDERER_TABLE on real inputs."""

    def test_mali_g720_immortalis_is_mid(self):
        """The .66 Vulkan device name is the headline test.

        Both spellings ("Mali-G720-Immortalis" with the brand and
        "Mali-G720" bare) must classify the same way — 'mid'.
        """
        self.assertEqual(class_from_renderer("Mali-G720-Immortalis"), "mid")
        self.assertEqual(class_from_renderer("Mali-G720"), "mid")
        # Lower-case — class_from_renderer uses re.IGNORECASE.
        self.assertEqual(class_from_renderer("mali-g720-immortalis"), "mid")

    def test_mali_g610_g715_are_mid_but_g78_is_unmatched(self):
        """Upstream behaviour, not what we want it to be.

        The upstream regex `mali-g[67]\\d\\d` requires TWO digits after
        the letter. That catches `Mali-G610`, `Mali-G615`, `Mali-G710`,
        `Mali-G715`, `Mali-G720` (all 'mid') but NOT `Mali-G78` (only
        one trailing digit). The 'weak' regex `mali-g[35]\\d` requires
        the same shape, so Mali-G78 returns None from the table.

        On the .66 host we only see Mali-G720-Immortalis, which IS
        matched (via the "immortalis" substring), so this regex gap
        doesn't affect .66's verdict. Locking the current behaviour
        here means a future change to fix the Mali-G78 gap is caught
        as a test update rather than a silent behaviour shift."""
        # Two-digit trailing matches -> mid via "mali-g[67]\\d\\d"
        for r in ("Mali-G610", "Mali-G615", "Mali-G710", "Mali-G715"):
            self.assertEqual(
                class_from_renderer(r), "mid",
                f"renderer {r!r} should be mid (matches mali-g[67]dd)",
            )
        # Single-digit trailing is unmatched by the current regex.
        self.assertIsNone(class_from_renderer("Mali-G78"))
        self.assertIsNone(class_from_renderer("Mali-G77"))
        # Mali-G52 -> weak via "mali-g[35]dd" (G5 starts with 5)
        self.assertEqual(class_from_renderer("Mali-G52"), "weak")

    def test_llvmpipe_is_weak(self):
        """The Mesa software renderer must classify as weak so a
        genuine software-render system isn't given 'mid' by mistake.

        This is the table-side contract; the refusal-aware fallback in
        classify_from_calibrator_result is what overrides this verdict
        when there is also real hardware bound (the bug fix).
        """
        self.assertEqual(class_from_renderer(LLVMIPE_RENDERER_V66), "weak")
        self.assertEqual(class_from_renderer("softpipe"), "weak")
        # NOTE: upstream RENDERER_TABLE does NOT match bare "swrast".
        # That's an upstream gap we deliberately do not change in this
        # branch (RENDERER_TABLE is reproduced verbatim from
        # /usr/bin/ncz-screensaver). A swrast-only renderer would
        # fall through to class_from_topology(None) -> weak, which
        # is the right answer for a software-only box.
        self.assertIsNone(class_from_renderer("swrast"))

    def test_intel_iris_xe_currently_returns_weak(self):
        """Upstream regex quirk: `iris(?! xe)` matches "Iris(R)"
        because the negative lookahead only sees "(R", not " xe".
        So Intel Iris Xe ends up classified as 'weak' today — that's
        an upstream bug we are NOT fixing in this branch (it would
        mean rewriting RENDERER_TABLE).

        Locked here so a future fix is caught."""
        self.assertEqual(
            class_from_renderer("Intel(R) Iris(R) Xe Graphics"), "weak"
        )

    def test_intel_uhd_is_weak(self):
        self.assertEqual(class_from_renderer("Mesa Intel(R) UHD Graphics 630 (KBL GT2)"), "weak")

    def test_nvidia_is_strong(self):
        for r in ("NVIDIA GeForce RTX 4090/PCIe/SSE2",
                  "NVIDIA TITAN Xp/PCIe/SSE2",
                  "Quadro RTX 8000/PCIe/SSE2"):
            self.assertEqual(class_from_renderer(r), "strong")

    def test_amd_navi14_is_mid_and_navi21_is_strong(self):
        """Upstream behaviour: `navi ?[1-9]\\d` matches Navi10, 21,
        23, etc. case insensitively (NAVI14 in the iGPU string
        matches, classifying the MBP 16" iGPU as 'strong'). That's
        an upstream bug (Navi14 iGPU is mid) we deliberately do NOT
        fix in this branch. Locked here so the regression test will
        catch a future fix."""
        # RX 6900 XT (Navi21 dGPU) is correctly strong.
        self.assertEqual(class_from_renderer(
            "Radeon RX 6900 XT (navi21, LLVM 19.1.7)"
        ), "strong")
        # Upstream BUG: the MBP 16" iGPU (Navi14) is currently
        # classified as 'strong' because the regex catches NAVI14.
        # We document the gap and assert current behaviour.
        self.assertEqual(class_from_renderer(
            "AMD Radeon Pro 5500M (NAVI14, DRM 3.59.0, 6.8.0-31-generic, LLVM 19.1.7)"
        ), "strong")

    def test_empty_string_returns_none(self):
        # No renderer reported AND no other info -> the launcher falls
        # through to class_from_topology, NOT class_from_renderer.
        self.assertIsNone(class_from_renderer(""))
        self.assertIsNone(class_from_renderer(None))


# ---------------------------------------------------------------------------
# class_from_topology — the refusal-fallback's last line of defence
# ---------------------------------------------------------------------------


class ClassFromTopologyTests(unittest.TestCase):
    """class_from_topology must return 'mid' for mali_kbase on .66."""

    def test_mali_kbase_on_v66_is_mid(self):
        """The sysfs-only answer for .66 is 'mid' — that's the floor
        of the bug fix: even without the launcher having a renderer
        string, a mali_kbase-bound system must be 'mid'."""
        topo = GpuTopology.from_dict({
            "driver": "mali_kbase",
            "discrete": False,
            "display": True,
        })
        self.assertEqual(class_from_topology(topo), "mid")
        self.assertEqual(class_from_topology_compat({
            "driver": "mali_kbase",
            "discrete": False,
            "display": True,
        }), "mid")

    def test_panthor_is_mid(self):
        """panthor is CIX's open driver alternative to mali_kbase."""
        topo = GpuTopology.from_dict({
            "driver": "panthor", "discrete": False, "display": True,
        })
        self.assertEqual(class_from_topology(topo), "mid")

    def test_amd_discrete_is_strong(self):
        topo = GpuTopology.from_dict({
            "driver": "amdgpu", "discrete": True, "display": True,
        })
        self.assertEqual(class_from_topology(topo), "strong")

    def test_intel_i915_is_weak(self):
        # iGPU Intel is weak per the table.
        topo = GpuTopology.from_dict({
            "driver": "i915", "discrete": False, "display": True,
        })
        self.assertEqual(class_from_topology(topo), "weak")

    def test_no_gpu_is_weak(self):
        """No GPU bound at all -> weak (server build, VM without a
        passthrough, etc.)."""
        self.assertEqual(class_from_topology(None), "weak")
        topo = GpuTopology.from_dict(None)
        self.assertEqual(class_from_topology(topo), "weak")
        topo = GpuTopology.from_dict({})
        self.assertEqual(class_from_topology(topo), "weak")


# ---------------------------------------------------------------------------
# classify_from_calibrator_result — the refusal-aware fix
# ---------------------------------------------------------------------------


class RefusalFallbackTests(unittest.TestCase):
    """The bug fix.

    On .66 before the fix: code=3 returned weak unconditionally, even
    though mali_kbase was bound and a real Mali-G720 was present. The
    launcher had no way to distinguish "refused because llvmpipe" from
    "refused because there is no GPU".

    After the fix: code=3 with hardware bound returns the topology
    class (mid for mali_kbase), preferring the renderer-table answer
    when a renderer string is known. code=3 with no hardware bound
    keeps the original 'weak' verdict.
    """

    def setUp(self):
        # The display GPU the launcher would enumerate on .66.
        self.v66_gpu = {
            "id": "card0",
            "driver": "mali_kbase",
            "discrete": False,
            "display": True,
            "vendor": "other",
            "device": "0",
        }
        # A software-only VM (no /dev/dri/card* at all).
        self.software_only_vm = None

    # -- Branch 1: calibration success ----------------------------------

    def test_calibration_success_with_benchmark_is_mid_on_v66(self):
        """The exact ms value captured on .66 (17.812) must come out
        as 'mid' with source='calibration'. This is the happy path —
        even before the bug fix this branch worked."""
        entry = classify_from_calibrator_result(
            code=EXIT_OK,
            gpu=self.v66_gpu,
            renderer_hint=MALI_DEVICE_NAME_V66,
            ms_hint=17.812,
            version_hint=MALI_EGL_VERSION_V66,
        )
        self.assertEqual(entry.cls, "mid")
        self.assertEqual(entry.source, "calibration")
        self.assertAlmostEqual(entry.ms, 17.812)
        self.assertFalse(entry.software)

    def test_calibration_success_with_zero_ms_falls_through(self):
        """Calibrator returned ms=0 (the benchmark sub-frame didn't
        time cleanly). The launcher treats this as 'no useful
        measurement' and tries a fallback. We use the renderer hint
        ('Mali-G720-Immortalis' from --identify) → 'mid'."""
        entry = classify_from_calibrator_result(
            code=EXIT_OK,
            gpu=self.v66_gpu,
            renderer_hint=MALI_DEVICE_NAME_V66,
            ms_hint=0.0,
            version_hint=MALI_EGL_VERSION_V66,
        )
        # Falls through to retry-later path because ms<=0.
        self.assertEqual(entry.source, "retry-later")
        # Class from renderer hint is mid (because Immortalis matches
        # row 4 of RENDERER_TABLE).
        self.assertEqual(entry.cls, "mid")

    # -- Branch 4: refusal + hardware bound  (THE FIX) -------------------

    def test_refused_with_mali_kbase_bound_returns_mid(self):
        """THE smoking-gun regression test.

        Pre-fix: this returned `{"class": "weak"}`.
        Post-fix: it must return `{"class": "mid"}` with
        source="refused-by-hardware-driver".

        If anyone reverts the fix, this test will fail loudly.
        """
        entry = classify_from_calibrator_result(
            code=EXIT_SOFTWARE_REFUSED,
            gpu=self.v66_gpu,
        )
        self.assertEqual(entry.cls, "mid")
        self.assertEqual(entry.source, "refused-by-hardware-driver")
        self.assertTrue(entry.software)
        # The note explains the path so the settings UI can surface it.
        self.assertIn("mali_kbase", entry.note)
        # The dict shape the launcher caches is preserved.
        d = entry.to_dict()
        self.assertEqual(d["class"], "mid")
        self.assertEqual(d["source"], "refused-by-hardware-driver")
        self.assertTrue(d["software"])

    def test_refused_with_mali_kbase_bound_prefers_renderer_hint(self):
        """When both the topology and a renderer string are available,
        the renderer-table answer wins because it is more informative.

        The RENDERER_TABLE puts Immortalis at mid (row 4) and mali_kbase
        topology is also mid, so they agree. The point of this test is
        to lock in the preference so a future change that makes the
        two diverge is caught (e.g. a hypothetical future mali_kbase
        variant that the table maps to weak)."""
        entry = classify_from_calibrator_result(
            code=EXIT_SOFTWARE_REFUSED,
            gpu=self.v66_gpu,
            renderer_hint="Mali-G720-Immortalis",
        )
        self.assertEqual(entry.cls, "mid")
        self.assertEqual(entry.source, "refused-by-hardware-driver")
        self.assertEqual(entry.renderer, "Mali-G720-Immortalis")

    def test_refused_with_mali_kbase_bound_no_renderer_falls_to_topology(self):
        """No renderer hint available (the .66 path). Topology says
        mid → verdict is mid."""
        entry = classify_from_calibrator_result(
            code=EXIT_SOFTWARE_REFUSED,
            gpu=self.v66_gpu,
        )
        self.assertEqual(entry.cls, "mid")

    def test_refused_with_mali_kbase_and_llvmpipe_renderer_hint(self):
        """THE smoking-gun case from .66, locked in.

        Pre-fix: a confused --identify that returned llvmpipe + a
        topology that says mali_kbase would have given 'weak' (the
        original code used `class_from_renderer(renderer) or
        class_from_topology(gpu)`, which took the renderer answer
        first). Post-fix: when topology says hardware is bound AND
        the renderer says 'weak', trust the topology (because
        'weak' here is the signature of a confused probe — the real
        evidence is /sys/class/misc/mali0/device/driver pointing at
        mali_kbase, not the GL_RENDERER string the calibrator got
        because of the wrong loader path).
        """
        entry = classify_from_calibrator_result(
            code=EXIT_SOFTWARE_REFUSED,
            gpu=self.v66_gpu,
            renderer_hint=LLVMIPE_RENDERER_V66,  # "llvmpipe (LLVM 21.1.8, 128 bits)"
        )
        self.assertEqual(entry.cls, "mid")
        self.assertEqual(entry.source, "refused-by-hardware-driver")

    def test_refused_with_mali_kbase_and_known_mali_renderer_hint(self):
        """When the renderer probe is RIGHT and equals the topology
        class, they agree."""
        entry = classify_from_calibrator_result(
            code=EXIT_SOFTWARE_REFUSED,
            gpu=self.v66_gpu,
            renderer_hint="Mali-G720-Immortalis",
        )
        self.assertEqual(entry.cls, "mid")

    def test_refused_with_mali_kbase_and_apple_m_renderer_hint(self):
        """If a future box had a mali_kbase AND a passthrough that
        happened to surface as Apple M2 (synthetic test case, but
        the rule matters), the more specific table answer wins."""
        entry = classify_from_calibrator_result(
            code=EXIT_SOFTWARE_REFUSED,
            gpu=self.v66_gpu,
            renderer_hint="Apple M2 Pro",
        )
        # Apple M2 -> 'strong' (matches row 1). Topology says 'mid'.
        # Renderer-table answer is more specific AND not 'weak' so it
        # wins.
        self.assertEqual(entry.cls, "strong")

    def test_refused_with_panthor_bound_returns_mid(self):
        """panthor is the open driver; same expected behaviour."""
        entry = classify_from_calibrator_result(
            code=EXIT_SOFTWARE_REFUSED,
            gpu={
                "driver": "panthor",
                "discrete": False,
                "display": True,
            },
        )
        self.assertEqual(entry.cls, "mid")
        self.assertEqual(entry.source, "refused-by-hardware-driver")

    def test_refused_with_intel_iris_xe_bound_returns_weak_today(self):
        """Upstream quirk: RENDERER_TABLE classifies "Intel(R) Iris(R)
        Xe Graphics" as 'weak' (the `iris(?! xe)` regex incorrectly
        matches "Iris(R)"). That means when the launcher sees an
        Iris Xe AND the calibrator refused AND no compositor was
        alive, the refusal-fallback ALSO returns weak today.

        Locked here. A future fix to RENDERER_TABLE that moves Iris
        Xe to mid will surface as a test update.
        """
        entry = classify_from_calibrator_result(
            code=EXIT_SOFTWARE_REFUSED,
            gpu={"driver": "xe", "discrete": False, "display": True},
            renderer_hint="Intel(R) Iris(R) Xe Graphics",
        )
        # Renderer says weak (because of the upstream regex quirk);
        # topology says weak; both agree → weak.
        self.assertEqual(entry.cls, "weak")
        # And the source is the refusal-fallback one (hardware bound).
        self.assertEqual(entry.source, "refused-by-hardware-driver")

    def test_refused_with_intel_uhd_bound_returns_weak(self):
        """Intel UHD 630 — the reference 'weak' GPU. Topography says
        weak AND the renderer-table would also say weak, so even if
        we didn't have the renderer hint, the verdict is weak."""
        entry = classify_from_calibrator_result(
            code=EXIT_SOFTWARE_REFUSED,
            gpu={"driver": "i915", "discrete": False, "display": True},
            renderer_hint="Mesa Intel(R) UHD Graphics 630 (KBL GT2)",
        )
        self.assertEqual(entry.cls, "weak")

    def test_refused_with_amd_discrete_returns_strong(self):
        """An RX 6900 XT bound to amdgpu. The refusal with discrete=True
        returns 'strong'."""
        entry = classify_from_calibrator_result(
            code=EXIT_SOFTWARE_REFUSED,
            gpu={"driver": "amdgpu", "discrete": True, "display": True},
        )
        self.assertEqual(entry.cls, "strong")
        self.assertEqual(entry.source, "refused-by-hardware-driver")

    # -- Branch 5: refusal with NO hardware bound (legitimate weak) ------

    def test_refused_with_no_gpu_at_all_returns_weak(self):
        """A real software-only machine (server, headless, container
        without passthrough). Refused, no hardware bound → 'weak'.
        This is the only case where the original behaviour is right."""
        entry = classify_from_calibrator_result(
            code=EXIT_SOFTWARE_REFUSED,
            gpu=self.software_only_vm,
        )
        self.assertEqual(entry.cls, "weak")
        self.assertEqual(entry.source, "refused")
        self.assertTrue(entry.software)

    def test_refused_with_only_display_controller_returns_weak(self):
        """A box with a display controller (linlondp, komeda) but no
        render GPU bound — the typical SDC-only media player. The
        linlondp driver is NOT in HARDWARE_GPU_DRIVERS, so it counts
        as 'no hardware GPU bound' and the verdict is weak.

        This case matters for the .66 host: its four DRM cards are
        all bound to linlondp; the actual renderer is /dev/mali0 via
        mali_kbase, NOT linlondp. So the launcher's list_gpus() picks
        up linlondp as a non-renderer and removes it from the
        display-GPU candidates, leaving the mali_kbase entry as the
        display GPU. The tests below model both situations."""
        entry = classify_from_calibrator_result(
            code=EXIT_SOFTWARE_REFUSED,
            gpu={"driver": "linlondp", "discrete": False, "display": False},
        )
        # linlondp is not in HARDWARE_GPU_DRIVERS, so this is the
        # "no hardware bound" branch.
        self.assertEqual(entry.cls, "weak")
        self.assertEqual(entry.source, "refused")

    # -- Branch 3: retry-able failure ------------------------------------

    def test_exit_no_config_with_hardware_falls_through(self):
        """eglInitialize_failed (EXIT_NO_CONFIG) is retry-able and
        the launcher caches for an hour. With hardware bound and a
        renderer hint we use the renderer-table answer; otherwise
        topology."""
        entry = classify_from_calibrator_result(
            code=EXIT_NO_CONFIG,
            gpu=self.v66_gpu,
            renderer_hint=MALI_DEVICE_NAME_V66,
        )
        self.assertEqual(entry.source, "retry-later")
        self.assertEqual(entry.cls, "mid")  # from renderer hint

    def test_exit_error_with_no_hardware_returns_weak(self):
        entry = classify_from_calibrator_result(
            code=EXIT_ERROR,
            gpu=self.software_only_vm,
        )
        self.assertEqual(entry.source, "retry-later")
        self.assertEqual(entry.cls, "weak")

    def test_exit_no_compositor_reserved(self):
        """The reserved EXIT_NO_COMPOSITOR code is documented but not
        emitted by the current C source. Treat it the same as
        EXIT_NO_CONFIG — retry-able."""
        entry = classify_from_calibrator_result(
            code=EXIT_NO_COMPOSITOR,
            gpu=self.v66_gpu,
            renderer_hint=MALI_DEVICE_NAME_V66,
        )
        self.assertEqual(entry.source, "retry-later")
        self.assertEqual(entry.cls, "mid")


# ---------------------------------------------------------------------------
# Regression coverage for the launcher's existing code paths we didn't
# break
# ---------------------------------------------------------------------------


class DictShapeCompatTests(unittest.TestCase):
    """The classifier's to_dict() must produce the keys the launcher
    cache and UI rely on."""

    def test_calibration_entry_has_required_keys(self):
        entry = ClassifierEntry(
            cls="mid", source="calibration", ms=17.812,
            renderer="Mali-G720-Immortalis",
            version="OpenGL ES 3.2 v1.r53p0",
        )
        d = entry.to_dict()
        self.assertEqual(d["class"], "mid")
        self.assertEqual(d["source"], "calibration")
        self.assertAlmostEqual(d["ms"], 17.812)
        self.assertEqual(d["renderer"], "Mali-G720-Immortalis")
        self.assertEqual(d["version"], "OpenGL ES 3.2 v1.r53p0")
        self.assertFalse(d["software"])
        self.assertNotIn("note", d)  # empty notes don't appear

    def test_refused_entry_has_required_keys(self):
        entry = classify_from_calibrator_result(
            code=EXIT_SOFTWARE_REFUSED,
            gpu={"driver": "mali_kbase", "discrete": False, "display": True},
        )
        d = entry.to_dict()
        self.assertEqual(d["class"], "mid")
        self.assertEqual(d["source"], "refused-by-hardware-driver")
        self.assertTrue(d["software"])
        self.assertEqual(d["driver"], "mali_kbase")  # extras key

    def test_software_only_entry_has_required_keys(self):
        entry = classify_from_calibrator_result(
            code=EXIT_SOFTWARE_REFUSED,
            gpu=None,
        )
        d = entry.to_dict()
        self.assertEqual(d["class"], "weak")
        self.assertEqual(d["source"], "refused")
        self.assertTrue(d["software"])


class TableContractTests(unittest.TestCase):
    """Lock down the regex / class pairs so any reordering or rename is
    caught as a test failure."""

    def test_table_is_tuple_of_pattern_class(self):
        self.assertIsInstance(RENDERER_TABLE, tuple)
        for row in RENDERER_TABLE:
            self.assertEqual(len(row), 2)
            pattern, cls = row
            self.assertIsInstance(pattern, str)
            self.assertIn(cls, ("weak", "mid", "strong"))

    def test_table_strong_before_mid_before_weak(self):
        """Order is critical: a renderer that matches both a strong and
        a weak regex (e.g. an Intel 'Iris Xe' which appears in both
        row 3 and row 4 patterns) must be classified by the FIRST
        match. Strong must come first, weak last."""
        classes_in_order = [cls for _, cls in RENDERER_TABLE]
        # At least one strong entry exists.
        self.assertIn("strong", classes_in_order)
        # And at least one weak entry exists.
        self.assertIn("weak", classes_in_order)
        # The first 'weak' must come AFTER the last 'strong'.
        last_strong = max(
            i for i, c in enumerate(classes_in_order) if c == "strong"
        )
        first_weak = next(
            i for i, c in enumerate(classes_in_order) if c == "weak"
        )
        self.assertGreater(first_weak, last_strong,
                           "weak must come AFTER strong in RENDERER_TABLE")


if __name__ == "__main__":
    unittest.main()