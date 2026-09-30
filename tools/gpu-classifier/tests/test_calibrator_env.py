# test_calibrator_env.py — unit tests for tools/gpu-classifier/calibrator_env.py.
#
# Standard library only. The Sky1-arm64 tests mock `is_sky1_arm64` and
# `sky1_loader_paths_available` to True; the existing-path checks
# (`is_dir()`, `is_file()`) on the real `CIXGPU_PRO_LIBDIR` /
# `CIXGPU_VENDOR_JSON` Path objects are NOT mocked away — they answer
# correctly on .66 (which is where these were developed) and on any host
# that has the cixgpu-pro package installed. On CI hosts that do not
# have the package, the `NoOpTests` cover the no-op behaviour.

from __future__ import annotations

import os
import sys
import unittest
import unittest.mock as mock
from pathlib import Path

_HERE = os.path.dirname(os.path.abspath(__file__))
_PKG_PARENT = os.path.dirname(_HERE)
if _PKG_PARENT not in sys.path:
    sys.path.insert(0, _PKG_PARENT)

from calibrator_env import (  # noqa: E402
    CIXGPU_PRO_LIBDIR,
    CIXGPU_VENDOR_JSON,
    augment_calibrator_env,
    augmented_calibrator_env,
    is_sky1_arm64,
    sky1_loader_paths_available,
)


class PlatformDetectionTests(unittest.TestCase):
    """is_sky1_arm64 / sky1_loader_paths_available must agree with the
    launcher's platform_id() but be safe on any machine."""

    def test_returns_bool(self):
        # Just make sure the function runs and returns a bool on this
        # test runner (x86_64 in CI). On this host is_sky1_arm64 must
        # be False.
        result = is_sky1_arm64()
        self.assertIsInstance(result, bool)

    def test_on_x86_64_is_false(self):
        # The CI runner is x86_64; on a non-aarch64 host is_sky1_arm64
        # MUST be False regardless of /proc/device-tree contents.
        if os.uname().machine != "aarch64":
            self.assertFalse(is_sky1_arm64())
            self.assertFalse(sky1_loader_paths_available())

    def test_acpi_cixh_path_is_detected(self):
        """The .66 host (MS-R1, NCZ-OS 26.7, kernel 7.3.0-rc5-sky1)
        does NOT have /proc/device-tree/compatible — it uses ACPI and
        the CIXH* entries show up under /sys/bus/acpi/devices. The
        detection MUST look there too or it'll return False on the
        very host it was written for."""
        with mock.patch("os.uname") as u:
            u.return_value = mock.Mock(machine="aarch64")
            with mock.patch.object(Path, "is_dir",
                                   new=mock.Mock(return_value=True)), \
                 mock.patch.object(Path, "iterdir",
                                   return_value=iter([
                                       Path("/sys/bus/acpi/devices/CIXH1003:00"),
                                       Path("/sys/bus/acpi/devices/ACPI0003:00"),
                                   ])):
                self.assertTrue(is_sky1_arm64())

    def test_device_tree_path_is_detected(self):
        """Pi-style device-tree systems (e.g. an arm64 board that uses
        device-tree instead of ACPI) must still be detected."""
        with mock.patch("os.uname") as u:
            u.return_value = mock.Mock(machine="aarch64")
            with mock.patch.object(Path, "is_dir",
                                   new=mock.Mock(return_value=False)), \
                 mock.patch.object(Path, "read_bytes",
                                   return_value=b"cix,sky1\0arm,vexpress\0"):
                self.assertTrue(is_sky1_arm64())


class NoOpTests(unittest.TestCase):
    """On any non-Sky1 host, or on Sky1 without cixgpu-pro installed,
    augment_calibrator_env must leave the env dict unchanged."""

    def test_no_op_on_non_sky1(self):
        env = {"LD_LIBRARY_PATH": "/usr/lib/x86_64-linux-gnu", "WAYLAND_DISPLAY": "wayland-0"}
        before = dict(env)
        result = augment_calibrator_env(env)
        self.assertEqual(env, before,
                         "augment_calibrator_env must not mutate env on non-Sky1")
        # Returns the same dict (mutated or not).
        self.assertIs(result, env)

    def test_no_op_when_libdir_absent(self):
        # Simulate Sky1 + no cixgpu-pro install by mocking the constants.
        with mock.patch.object(sys.modules["calibrator_env"], "CIXGPU_PRO_LIBDIR",
                              new=Path("/no/such/dir")), \
             mock.patch.object(sys.modules["calibrator_env"], "is_sky1_arm64",
                              return_value=True):
            env = {"PATH": "/usr/bin"}
            before = dict(env)
            augment_calibrator_env(env)
            self.assertEqual(env, before)

    def test_no_op_when_libmali_so_missing(self):
        # Sky1 + cixgpu-pro dir present but libmali.so.0 absent (torn upgrade).
        # Force sky1_loader_paths_available to False so the helper short-
        # circuits before it can ever call is_dir() / is_file() on the
        # mocked Path.
        with mock.patch.object(sys.modules["calibrator_env"], "CIXGPU_PRO_LIBDIR",
                              new=Path("/tmp")), \
             mock.patch.object(sys.modules["calibrator_env"], "is_sky1_arm64",
                              return_value=True), \
             mock.patch.object(sys.modules["calibrator_env"],
                               "sky1_loader_paths_available",
                               return_value=False):
            env = {}
            before = dict(env)
            augment_calibrator_env(env)
            self.assertEqual(env, before)


class Sky1EnvAugmentationTests(unittest.TestCase):
    """On Sky1 with cixgpu-pro installed, the four keys must be set
    correctly. The exact libdir path is the one captured on .66."""

    def _sky1_with_cixgpu(self):
        """Mock the world so augment_calibrator_env thinks it's on
        Sky1 with the cixgpu-pro install present. Each test that uses
        this must patch the libdir existence check."""
        return (
            mock.patch.object(sys.modules["calibrator_env"], "is_sky1_arm64",
                              return_value=True),
            mock.patch.object(sys.modules["calibrator_env"],
                              "sky1_loader_paths_available",
                              return_value=True),
        )

    def test_sets_ld_library_path_on_empty_env(self):
        with self._sky1_with_cixgpu()[0], self._sky1_with_cixgpu()[1]:
            env = {}
            augment_calibrator_env(env)
            self.assertEqual(env["LD_LIBRARY_PATH"], str(CIXGPU_PRO_LIBDIR))

    def test_prepends_ld_library_path_to_existing(self):
        # The launcher's compositor env might already have a path on
        # LD_LIBRARY_PATH; we must not lose it.
        with self._sky1_with_cixgpu()[0], self._sky1_with_cixgpu()[1]:
            env = {"LD_LIBRARY_PATH": "/some/compositor/path"}
            augment_calibrator_env(env)
            self.assertTrue(env["LD_LIBRARY_PATH"].startswith(str(CIXGPU_PRO_LIBDIR)))
            self.assertIn("/some/compositor/path", env["LD_LIBRARY_PATH"])

    def test_does_not_double_prepend(self):
        with self._sky1_with_cixgpu()[0], self._sky1_with_cixgpu()[1]:
            existing = str(CIXGPU_PRO_LIBDIR) + ":/other/path"
            env = {"LD_LIBRARY_PATH": existing}
            augment_calibrator_env(env)
            self.assertEqual(env["LD_LIBRARY_PATH"], existing)

    def test_sets_vendor_pin_if_file_exists(self):
        with self._sky1_with_cixgpu()[0], self._sky1_with_cixgpu()[1], \
             mock.patch.object(sys.modules["calibrator_env"], "CIXGPU_VENDOR_JSON",
                               new=Path("/tmp/fake-vendor.json")), \
             mock.patch("os.path.isfile", return_value=True):
            env = {}
            augment_calibrator_env(env)
            self.assertEqual(env["__EGL_VENDOR_LIBRARY_FILENAMES"], "/tmp/fake-vendor.json")

    def test_skips_vendor_pin_when_file_missing(self):
        """If 40_cix.json is missing (the file lives in the separate
        cixgpu-compat package; some operator setups do that), do not
        set a broken path."""
        with self._sky1_with_cixgpu()[0], self._sky1_with_cixgpu()[1], \
             mock.patch("os.path.isfile", return_value=False):
            env = {}
            augment_calibrator_env(env)
            self.assertNotIn("__EGL_VENDOR_LIBRARY_FILENAMES", env)

    def test_does_not_clobber_existing_vendor_pin(self):
        """If a compositor's vendor pin is already set (the launcher's
        drop_egl_vendor_pins() doesn't pop it for the calibrator), do
        not replace it."""
        with self._sky1_with_cixgpu()[0], self._sky1_with_cixgpu()[1]:
            env = {"__EGL_VENDOR_LIBRARY_FILENAMES": "/op/path/40_cix.json"}
            augment_calibrator_env(env)
            self.assertEqual(env["__EGL_VENDOR_LIBRARY_FILENAMES"], "/op/path/40_cix.json")

    def test_sets_ncz_gpu_backend_to_mali(self):
        with self._sky1_with_cixgpu()[0], self._sky1_with_cixgpu()[1]:
            env = {}
            augment_calibrator_env(env)
            self.assertEqual(env["NCZ_GPU_BACKEND"], "mali")

    def test_does_not_clobber_existing_ncz_gpu_backend(self):
        with self._sky1_with_cixgpu()[0], self._sky1_with_cixgpu()[1]:
            env = {"NCZ_GPU_BACKEND": "panthor"}
            augment_calibrator_env(env)
            self.assertEqual(env["NCZ_GPU_BACKEND"], "panthor")

    def test_sets_surfaceless_when_no_wayland_or_display(self):
        """The headline behavioural test for .66.

        Pre-fix: no WAYLAND_DISPLAY / no DISPLAY / no compositor -> the
        calibrator subprocess ran with no __EGL_PLATFORM override, the
        default platform picked ZINK + llvmpipe, and code=3 came back.
        Post-fix: with no Wayland and no X11, we set __EGL_PLATFORM=
        surfaceless so the calibrator's eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_EXT, ...)
        succeeds against /dev/mali0 directly without needing a
        Wayland compositor or an X server."""
        with self._sky1_with_cixgpu()[0], self._sky1_with_cixgpu()[1]:
            env = {}
            augment_calibrator_env(env)
            self.assertEqual(env["__EGL_PLATFORM"], "surfaceless")

    def test_does_not_set_surfaceless_when_wayland_display_present(self):
        """If WAYLAND_DISPLAY is set (a compositor IS running), we
        MUST NOT default to surfaceless — the compositor's platform
        (default or wayland) is the right one."""
        with self._sky1_with_cixgpu()[0], self._sky1_with_cixgpu()[1]:
            env = {"WAYLAND_DISPLAY": "wayland-0"}
            augment_calibrator_env(env)
            self.assertNotIn("__EGL_PLATFORM", env)

    def test_does_not_set_surfaceless_when_display_present(self):
        """Same for X11 (labwc with XWayland, X session)."""
        with self._sky1_with_cixgpu()[0], self._sky1_with_cixgpu()[1]:
            env = {"DISPLAY": ":0"}
            augment_calibrator_env(env)
            self.assertNotIn("__EGL_PLATFORM", env)

    def test_honours_explicit_egl_platform(self):
        """If the launcher (or operator) already provided an explicit
        __EGL_PLATFORM, leave it alone — they know what they're doing
        (e.g. a CI run that wants gbm for headless GLES)."""
        with self._sky1_with_cixgpu()[0], self._sky1_with_cixgpu()[1]:
            env = {"__EGL_PLATFORM": "gbm"}
            augment_calibrator_env(env)
            self.assertEqual(env["__EGL_PLATFORM"], "gbm")


class Sky1AugmentedFullStackTests(unittest.TestCase):
    """End-to-end: on Sky1 with cixgpu-pro present, the full set of
    keys comes out exactly as the .66 live probe showed."""

    def test_full_stack_matches_v66_probe(self):
        with mock.patch.object(sys.modules["calibrator_env"], "is_sky1_arm64",
                               return_value=True), \
             mock.patch.object(sys.modules["calibrator_env"],
                               "sky1_loader_paths_available",
                               return_value=True), \
             mock.patch("os.path.isfile", return_value=True):
            env = {}
            augment_calibrator_env(env)
            # This is the dict we proved works on .66 by hand:
            #   LD_LIBRARY_PATH=/opt/cixgpu-pro/lib/aarch64-linux-gnu \
            #   __EGL_VENDOR_LIBRARY_FILENAMES=/opt/cixgpu-compat/share/glvnd/egl_vendor.d/40_cix.json \
            #   NCZ_GPU_BACKEND=mali \
            #   __EGL_PLATFORM=surfaceless \
            #   /usr/libexec/.../ncz-screensaver-calibrate --identify
            #   =>  {"renderer":"Mali-G720-Immortalis", ...}
            self.assertEqual(env["LD_LIBRARY_PATH"], str(CIXGPU_PRO_LIBDIR))
            self.assertEqual(env["__EGL_VENDOR_LIBRARY_FILENAMES"], str(CIXGPU_VENDOR_JSON))
            self.assertEqual(env["NCZ_GPU_BACKEND"], "mali")
            self.assertEqual(env["__EGL_PLATFORM"], "surfaceless")


class AugmentedFromOsEnvironTests(unittest.TestCase):
    """augmented_calibrator_env() returns a fresh dict from os.environ
    (or a base mapping), never None."""

    def test_returns_dict(self):
        self.assertIsInstance(augmented_calibrator_env(), dict)

    def test_returns_dict_from_base(self):
        base = {"HOME": "/tmp", "LANG": "C"}
        out = augmented_calibrator_env(base)
        self.assertEqual(out["HOME"], "/tmp")
        self.assertEqual(out["LANG"], "C")

    def test_base_not_mutated(self):
        base = {"HOME": "/tmp"}
        base_snapshot = dict(base)
        augmented_calibrator_env(base)
        self.assertEqual(base, base_snapshot)


if __name__ == "__main__":
    unittest.main()