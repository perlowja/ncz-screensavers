"""tests/test_launcher_integration.py — integration tests for the patched
launcher.

These tests load the patched launcher (gpu_classifier imports enabled)
and exercise the gpu_class() function with faked calibrator subprocess
results, simulating the actual /sys state on .66.

Run from tools/gpu-classifier:

    python3 -m unittest discover tests -v

Stdlib only. The patched launcher is loaded from
tests/fixtures/patched_launcher.py so we don't depend on
/usr/bin/ncz-screensaver being present.
"""

from __future__ import annotations

import importlib.machinery
import importlib.util
import sys
import unittest
from pathlib import Path
from unittest import mock

# Make the package importable
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

# The patched launcher lives at tests/fixtures/patched_launcher.py and
# is a copy of /usr/bin/ncz-screensaver with our patch applied. We
# import it as a module and rebind sys.argv so its argparse main() is
# skipped (we only want to call gpu_class() etc.).
_FIXTURE = Path(__file__).resolve().parent / "fixtures" / "patched_launcher.py"
if not _FIXTURE.is_file():
    raise unittest.SkipTest(
        f"fixture not present at {_FIXTURE}; "
        f"build it with `bash tests/build_patched_launcher_fixture.sh`"
    )


def _load_patched_launcher():
    """Load the patched launcher as a module. Insert the fixtures dir
    on sys.path so the launcher's `from gpu_classifier import ...`
    works."""
    # The launcher does `from gpu_classifier import ...`; that import
    # will find gpu_classifier on sys.path. We already added the
    # parent of gpu_classifier (the package root) above, so the
    # launcher can find it. Insert the fixtures dir first so that
    # 'patched_launcher' resolves to our fixture.
    fixtures_dir = str(_FIXTURE.parent)
    if fixtures_dir not in sys.path:
        sys.path.insert(0, fixtures_dir)
    loader = importlib.machinery.SourceFileLoader("patched_launcher", str(_FIXTURE))
    spec = importlib.util.spec_from_loader("patched_launcher", loader)
    m = importlib.util.module_from_spec(spec)
    sys.modules["patched_launcher"] = m
    sys.argv = ["ncz-screensaver"]  # silence argparse on import
    loader.exec_module(m)
    return m


# Real /sys data from .66 (mirrors what discover_gpus() returns there)
V66_ALL_GPUS = [
    {
        "id": "pci-CIXH5010_00",
        "card": "card0",
        "slot": "CIXH5010:00",
        "vendor": "other",
        "driver": "linlondp",
        "device": "",
        "display": False,
        "boot_vga": False,
        "discrete": False,
        "render": True,
        "sysfs_kind": "drm",
    },
    {
        "id": "pci-CIXH5010_01",
        "card": "card1",
        "slot": "CIXH5010:01",
        "vendor": "other",
        "driver": "linlondp",
        "device": "",
        "display": False,
        "boot_vga": False,
        "discrete": False,
        "render": True,
        "sysfs_kind": "drm",
    },
    {
        "id": "pci-CIXH5010_03",
        "card": "card2",
        "slot": "CIXH5010:03",
        "vendor": "other",
        "driver": "linlondp",
        "device": "",
        "display": True,
        "boot_vga": False,
        "discrete": False,
        "render": True,
        "sysfs_kind": "drm",
    },
    {
        "id": "pci-CIXH5010_04",
        "card": "card3",
        "slot": "CIXH5010:04",
        "vendor": "other",
        "driver": "linlondp",
        "device": "",
        "display": False,
        "boot_vga": False,
        "discrete": False,
        "render": True,
        "sysfs_kind": "drm",
    },
    {
        "id": "soc-mali0",
        "card": "",
        "slot": "mali0",
        "vendor": "arm",
        "driver": "mali",
        "device": "",
        "display": False,
        "boot_vga": False,
        "discrete": False,
        "render": True,
        "sysfs_kind": "misc",
    },
]


class PatchedLauncherTests(unittest.TestCase):
    """End-to-end through the patched launcher's gpu_class()."""

    @classmethod
    def setUpClass(cls):
        cls.m = _load_patched_launcher()

    def test_patched_launcher_imports_gpu_classifier(self):
        """The try/except import succeeded, so the launcher's
        classify_from_calibrator_result binding points at our gpu_classifier
        implementation. We can't `assertIs` because the `from X import Y`
        creates a separate name binding; instead we verify the function is
        the real one (not the None-returning stub) by checking that calling
        it on a known input returns a ClassifierEntry-shaped result."""
        # The stub returns None; the real one returns a ClassifierEntry.
        result = self.m.classify_from_calibrator_result(
            code=3,
            gpu={"driver": "mali", "discrete": False, "display": False},
            renderer_hint="Mali-G720-Immortalis",
        )
        self.assertIsNotNone(result)
        # Same for the other two — call and verify they don't crash and
        # produce the right shape.
        env = self.m.augment_calibrator_env({})
        self.assertIsInstance(env, dict)
        gpus = self.m.discover_gpus()
        self.assertIsInstance(gpus, list)

    def test_all_known_gpus_merges_drm_and_misc(self):
        """On .66 the union has 4 DRM + 1 misc = 5 entries."""
        with (
            mock.patch.object(self.m, "list_gpus", return_value=V66_ALL_GPUS[:4]),
            mock.patch.object(self.m, "discover_gpus", return_value=[V66_ALL_GPUS[4]]),
        ):
            gpus = self.m._all_known_gpus()
            self.assertEqual(len(gpus), 5)
            ids = {g["id"] for g in gpus}
            self.assertIn("soc-mali0", ids)
            self.assertIn("pci-CIXH5010_03", ids)

    def test_augment_calibrator_env_on_v66(self):
        """The Sky1 loader path augmentation runs on .66 (which IS
        sky1-arm64)."""
        import calibrator_env as ce

        with (
            mock.patch.object(ce, "is_sky1_arm64", return_value=True),
            mock.patch.object(ce, "sky1_loader_paths_available", return_value=True),
        ):
            out = self.m.augment_calibrator_env({})
            self.assertIn("LD_LIBRARY_PATH", out)
            self.assertIn("/opt/cixgpu-pro", out["LD_LIBRARY_PATH"])
            self.assertEqual(out["NCZ_GPU_BACKEND"], "mali")
            self.assertEqual(out["__EGL_PLATFORM"], "surfaceless")

    def test_augment_calibrator_env_is_noop_off_sky1(self):
        """Not on Sky1 -> no augmentation."""
        import calibrator_env as ce

        with mock.patch.object(ce, "is_sky1_arm64", return_value=False):
            out = self.m.augment_calibrator_env({})
            self.assertEqual(out, {})

    def test_gpu_class_with_calibrator_refusal_on_v66_returns_mid(self):
        """THE smoking-gun end-to-end test.

        Pre-fix: code=3 with linlondp display_gpu -> unconditional weak.
        Post-fix: code=3 with linlondp display_gpu + misc/mali0 in
        _all_known_gpus -> mid (the misc entry is the real GPU).
        """
        # Mock the calibrator subprocess to return code=3 (refusal)
        # and no JSON; mock list_gpus() and _all_known_gpus() to
        # return the .66 GPU set.
        display_gpu = V66_ALL_GPUS[2]  # pci-CIXH5010_03 (display=True)
        with (
            mock.patch.object(self.m, "_calibrator_json", return_value=(3, None)),
            mock.patch.object(self.m, "list_gpus", return_value=V66_ALL_GPUS[:4]),
            mock.patch.object(self.m, "build_child_env", return_value={}),
            mock.patch.object(self.m, "gpu_env", return_value={}),
            mock.patch.object(self.m, "read_class_cache", return_value={}),
            mock.patch.object(self.m, "_all_known_gpus", return_value=V66_ALL_GPUS),
            mock.patch.object(
                self.m, "class_cache_path", return_value=Path("/tmp/empty-cache.json")
            ),
            mock.patch.object(self.m, "log"),
        ):
            # force=True to skip the cache hit path
            result = self.m.gpu_class(
                settings={"pool-gpu-class": "auto"},
                gpu=display_gpu,
                run=True,
                force=True,
            )
        self.assertEqual(result["class"], "mid")
        self.assertEqual(result["source"], "refused-by-hardware-driver")
        # The driver recorded in extras should be "mali" (from misc entry)
        self.assertEqual(result["driver"], "mali")

    def test_gpu_class_with_calibrator_exit_2_on_v66_returns_mid(self):
        """The actual reproducer on .66: code=2 (no GLES3 config) with
        the misc/mali0 entry passed via _all_known_gpus. Post-fix
        verdict is mid."""
        display_gpu = V66_ALL_GPUS[2]
        with (
            mock.patch.object(self.m, "_calibrator_json", return_value=(2, None)),
            mock.patch.object(self.m, "list_gpus", return_value=V66_ALL_GPUS[:4]),
            mock.patch.object(self.m, "build_child_env", return_value={}),
            mock.patch.object(self.m, "gpu_env", return_value={}),
            mock.patch.object(self.m, "read_class_cache", return_value={}),
            mock.patch.object(self.m, "_all_known_gpus", return_value=V66_ALL_GPUS),
            mock.patch.object(
                self.m, "class_cache_path", return_value=Path("/tmp/empty-cache.json")
            ),
            mock.patch.object(self.m, "log"),
        ):
            result = self.m.gpu_class(
                settings={"pool-gpu-class": "auto"},
                gpu=display_gpu,
                run=True,
                force=True,
            )
        self.assertEqual(result["class"], "mid")
        self.assertEqual(result["source"], "retry-later")
        # The effective driver is the misc entry
        self.assertEqual(result.get("effective_driver"), "mali")
        self.assertEqual(result.get("sysfs_kind"), "misc")

    def test_gpu_class_with_calibrator_success_on_v66_returns_mid(self):
        """Happy-path: calibrator returns ms=9.88, the live .66
        benchmark. Verdict is mid via class_from_ms."""
        display_gpu = V66_ALL_GPUS[2]
        # First call: --identify returns the renderer
        # Second call: full benchmark returns ms=9.88
        calibrator_results = [
            (
                0,
                {
                    "renderer": "Mali-G720-Immortalis",
                    "version": "OpenGL ES 3.2 v1.r53p0-00eac0...",
                    "ms": 0,
                },
            ),
            (
                0,
                {
                    "renderer": "Mali-G720-Immortalis",
                    "version": "OpenGL ES 3.2 v1.r53p0-00eac0...",
                    "ms": 9.88,
                    "timer": "gpu",
                },
            ),
        ]
        with (
            mock.patch.object(
                self.m, "_calibrator_json", side_effect=calibrator_results
            ),
            mock.patch.object(self.m, "list_gpus", return_value=V66_ALL_GPUS[:4]),
            mock.patch.object(self.m, "build_child_env", return_value={}),
            mock.patch.object(self.m, "gpu_env", return_value={}),
            mock.patch.object(self.m, "read_class_cache", return_value={}),
            mock.patch.object(
                self.m, "class_cache_path", return_value=Path("/tmp/empty-cache.json")
            ),
            mock.patch.object(self.m, "atomic_write"),
            mock.patch.object(self.m, "log"),
        ):
            result = self.m.gpu_class(
                settings={"pool-gpu-class": "auto"},
                gpu=display_gpu,
                run=True,
                force=True,
            )
        self.assertEqual(result["class"], "mid")
        self.assertEqual(result["source"], "calibration")
        self.assertEqual(result["ms"], 9.88)


if __name__ == "__main__":
    unittest.main()


class FallbackEntryTests(unittest.TestCase):
    """The patched launcher's fallback_entry() must consult the misc
    GPU entry, not just the display_gpu."""

    @classmethod
    def setUpClass(cls):
        cls.m = _load_patched_launcher()

    def test_fallback_entry_with_no_renderer_uses_misc_entry(self):
        """Pre-fix: fallback_entry("", display_gpu) returns weak on .66.
        Post-fix: fallback_entry consults discover_gpus() and returns
        'mid' when the misc entry is a real hardware GPU."""
        display_gpu = V66_ALL_GPUS[2]
        with mock.patch.object(self.m, "discover_gpus", return_value=[V66_ALL_GPUS[4]]):
            result = self.m.fallback_entry("", display_gpu)
        self.assertEqual(result["class"], "mid")
        self.assertEqual(result["source"], "table")

    def test_fallback_entry_with_mali_renderer_returns_mid(self):
        """The cached renderer is 'Mali-G720-Immortalis' which matches
        the row-4 mid regex; fallback_entry must trust it."""
        display_gpu = V66_ALL_GPUS[2]
        with mock.patch.object(self.m, "discover_gpus", return_value=[V66_ALL_GPUS[4]]):
            result = self.m.fallback_entry("Mali-G720-Immortalis", display_gpu)
        self.assertEqual(result["class"], "mid")

    def test_fallback_entry_with_llvmpipe_renderer_uses_topology(self):
        """When the renderer says 'llvmpipe' (weak via the table), but
        a real GPU is bound, topology wins and returns 'mid'."""
        display_gpu = V66_ALL_GPUS[2]
        with mock.patch.object(self.m, "discover_gpus", return_value=[V66_ALL_GPUS[4]]):
            result = self.m.fallback_entry(
                "llvmpipe (LLVM 21.1.8, 128 bits)", display_gpu
            )
        self.assertEqual(result["class"], "mid")

    def test_fallback_entry_software_only_returns_weak(self):
        """Real software-only machine: no misc entry, no real GPU.
        Verdict must be weak."""
        with mock.patch.object(self.m, "discover_gpus", return_value=[]):
            result = self.m.fallback_entry(
                "",
                {"driver": "linlondp", "discrete": False, "display": True},
            )
        self.assertEqual(result["class"], "weak")


if __name__ == "__main__":
    unittest.main()
