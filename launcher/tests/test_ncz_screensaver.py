"""Unit and lifecycle tests for launcher/ncz-screensaver (no Wayland needed).

Run: python3 -m unittest discover -s launcher/tests -v
"""

import importlib.machinery
import importlib.util
import json
import os
import shutil
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LAUNCHER = ROOT / "launcher" / "ncz-screensaver"
SCHEMA_XML = ROOT / "config" / "dev.ncz.screensaver.gschema.xml"

_loader = importlib.machinery.SourceFileLoader("ncz_screensaver", str(LAUNCHER))
_spec = importlib.util.spec_from_loader("ncz_screensaver", _loader)
ns = importlib.util.module_from_spec(_spec)
_loader.exec_module(ns)


class ParserTests(unittest.TestCase):
    def test_gvariant(self):
        self.assertIs(ns.parse_gvariant("true"), True)
        self.assertIs(ns.parse_gvariant("false"), False)
        self.assertEqual(ns.parse_gvariant("300"), 300)
        self.assertEqual(ns.parse_gvariant("uint32 5"), 5)
        self.assertEqual(ns.parse_gvariant("'kip\\'s'"), "kip's")
        self.assertEqual(ns.parse_gvariant("@as []"), [])
        self.assertEqual(ns.parse_gvariant("['a', 'b_c']"), ["a", "b_c"])

    def test_gvariant_text_roundtrip(self):
        self.assertEqual(ns.gvariant_text(["a", "b"]), "['a', 'b']")
        self.assertEqual(ns.gvariant_text(True), "true")
        self.assertEqual(ns.gvariant_text(7), "7")

    def test_frame_is_black(self):
        hdr = b"P6\n4 4\n255\n"
        self.assertTrue(ns.frame_is_black(hdr + bytes(48)))
        self.assertFalse(ns.frame_is_black(hdr + bytes([200]) * 48))
        # Truncated or malformed frames must not hang or read as black.
        self.assertFalse(ns.frame_is_black(b"P6\n4"))
        self.assertFalse(ns.frame_is_black(b""))
        self.assertFalse(ns.frame_is_black(b"P6\n# unterminated"))
        self.assertFalse(ns.frame_is_black(hdr + bytes(10)))

    def test_frame_pixels_that_look_like_whitespace(self):
        # First pixel bytes 0x20 0x09 0x0a are data, not header whitespace.
        frame = b"P6\n1 1\n255\n" + bytes([32, 9, 10])
        self.assertFalse(ns.frame_is_black(frame))


class EnvTests(unittest.TestCase):
    def test_blackhole_color_mapping(self):
        base = {"XDG_RUNTIME_DIR": "/nonexistent", "NCZ_BLACKHOLE_COLORS": "old"}
        env = ns.build_child_env(
            "blackhole_gles3",
            {"blackhole-color-mode": "stylized"},
            base,
            "/nonexistent",
        )
        self.assertNotIn("NCZ_BLACKHOLE_COLORS", env)
        env = ns.build_child_env(
            "blackhole_gles3",
            {"blackhole-color-mode": "kipthorne"},
            base,
            "/nonexistent",
        )
        self.assertEqual(env["NCZ_BLACKHOLE_COLORS"], "kipthorne")
        env = ns.build_child_env(
            "voronoi_gles3",
            {"blackhole-color-mode": "kipthorne"},
            {"XDG_RUNTIME_DIR": "/x"},
            "/nonexistent",
        )
        self.assertNotIn("NCZ_BLACKHOLE_COLORS", env)
        self.assertEqual(env["NCZ_SCREENSAVER_ACTIVE"], "1")

    def test_gpu_offload_only_on_request(self):
        base = {
            "XDG_RUNTIME_DIR": "/x",
            "DRI_PRIME": "1",
            "__NV_PRIME_RENDER_OFFLOAD": "1",
        }
        off = ns.build_child_env(
            "x_gles3", {"gpu-offload": "off"}, base, "/nonexistent"
        )
        for name in ns.OFFLOAD_VARS:
            self.assertNotIn(name, off)
        prime = ns.build_child_env(
            "x_gles3",
            {"gpu-offload": "prime"},
            {"XDG_RUNTIME_DIR": "/x"},
            "/nonexistent",
        )
        self.assertEqual(prime["__NV_PRIME_RENDER_OFFLOAD"], "1")
        self.assertEqual(prime["__VK_LAYER_NV_optimus"], "NVIDIA_only")
        self.assertNotIn("__EGL_VENDOR_LIBRARY_FILENAMES", prime)
        self.assertNotIn("DRI_PRIME", prime)

    def test_offload_vars_never_copied_from_compositor(self):
        with tempfile.TemporaryDirectory() as tmp:
            proc = Path(tmp) / "7"
            proc.mkdir()
            (proc / "comm").write_text("labwc\n")
            (proc / "environ").write_bytes(
                b"__NV_PRIME_RENDER_OFFLOAD=1\0DRI_PRIME=1\0"
            )
            env = ns.build_child_env(
                "x_gles3", {"gpu-offload": "off"}, {"XDG_RUNTIME_DIR": "/x"}, tmp
            )
        self.assertNotIn("__NV_PRIME_RENDER_OFFLOAD", env)
        self.assertNotIn("DRI_PRIME", env)

    def test_compositor_gpu_env_import(self):
        with tempfile.TemporaryDirectory() as tmp:
            proc = Path(tmp) / "4242"
            proc.mkdir()
            (proc / "comm").write_text("labwc\n")
            (proc / "environ").write_bytes(
                b"__EGL_VENDOR_LIBRARY_FILENAMES=/x/40.json\0DRI_PRIME=1\0LD_LIBRARY_PATH=/opt/lib\0"
                b"MY_TOKEN=abc\0HOME=/h\0"
            )
            env = ns.build_child_env(
                "x_gles3", {}, {"XDG_RUNTIME_DIR": "/x", "HOME": "/me"}, tmp
            )
        self.assertEqual(env["__EGL_VENDOR_LIBRARY_FILENAMES"], "/x/40.json")
        self.assertEqual(env["LD_LIBRARY_PATH"], "/opt/lib")
        self.assertNotIn("DRI_PRIME", env)
        self.assertEqual(env["HOME"], "/me")  # the launcher's own env wins
        self.assertNotIn("MY_TOKEN", env)


@unittest.skipUnless(
    shutil.which("gsettings") and shutil.which("glib-compile-schemas"),
    "gsettings needed",
)
class LifecycleTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="ncz-ss-test-")
        t = Path(self.tmp)
        (t / "bin").mkdir()
        (t / "schemas").mkdir()
        shutil.copy(SCHEMA_XML, t / "schemas")
        subprocess.run(["glib-compile-schemas", str(t / "schemas")], check=True)
        for name, body in (
            ("ok1", "exec sleep 60"),
            ("ok2", "exec sleep 60"),
            ("crash", "exit 1"),
        ):
            f = t / "bin" / name
            f.write_text(f"#!/bin/sh\n{body}\n")
            f.chmod(0o755)
        (t / "cat.tsv").write_text("ok1\tOK1\tg\tg\nok2\tOK2\tg\tg\ncrash\tC\tg\tg\n")
        self.env = dict(
            os.environ,
            GSETTINGS_BACKEND="keyfile",
            GSETTINGS_SCHEMA_DIR=str(t / "schemas"),
            XDG_CONFIG_HOME=str(t / "cfg"),
            XDG_RUNTIME_DIR=str(t / "rt"),
            XDG_STATE_HOME=str(t / "st"),
            NCZ_SCREENSAVER_DIRS=str(t / "bin"),
            NCZ_SCREENSAVER_CATALOG=str(t / "cat.tsv"),
            NCZ_SCREENSAVER_MIN_CYCLE="1",
            NCZ_SCREENSAVER_PROC_ROOT=str(t / "noproc"),
        )
        os.makedirs(self.env["XDG_RUNTIME_DIR"], mode=0o700)

    def tearDown(self):
        self.cli("stop")
        shutil.rmtree(self.tmp, ignore_errors=True)

    def cli(self, *args, timeout=20):
        return subprocess.run(
            [str(LAUNCHER), *args],
            env=self.env,
            capture_output=True,
            text=True,
            timeout=timeout,
            check=False,
        )

    def status(self):
        p = self.cli("status", "--json")
        return json.loads(p.stdout)

    def wait(self, pred, timeout=8.0):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if pred():
                return True
            time.sleep(0.1)
        return False

    def test_lifecycle_and_single_instance(self):
        self.assertEqual(self.cli("set-hack", "ok1").returncode, 0)
        self.assertEqual(self.cli("status").returncode, 3)
        self.cli("start")
        self.assertTrue(self.wait(lambda: self.status()["running"]))
        self.assertEqual(self.status()["hack"], "ok1")
        self.assertIn("already running", self.cli("start").stdout)
        self.cli("stop")
        self.assertTrue(self.wait(lambda: not self.status()["running"]))
        self.assertEqual(self.cli("status").returncode, 3)

    def test_mode_off_starts_nothing_without_force(self):
        self.cli("set-mode", "off")
        self.cli("start", "--idle")
        self.assertFalse(self.status()["running"])

    def test_crash_fallback_via_playlist(self):
        self.cli("config", "set", "verify-render", "false")
        self.cli("config", "set", "playlist", "crash,ok2")
        self.cli("set-mode", "playlist")
        self.cli("start")
        self.assertTrue(self.wait(lambda: self.status().get("hack") == "ok2", 12.0))

    def test_gives_up_after_three_failures(self):
        self.cli("config", "set", "random-hacks", "crash")
        self.cli("set-mode", "random")
        p = self.cli("start", "--foreground", "--seconds", "20", timeout=30)
        self.assertEqual(p.returncode, 1)

    def test_playlist_rotation(self):
        self.cli("config", "set", "verify-render", "false")
        self.cli("config", "set", "playlist", "ok1,ok2")
        self.cli("config", "set", "cycle-delay", "5")
        self.cli("set-mode", "playlist")
        self.cli("start", "--seconds", "20")
        seen = []
        end = time.monotonic() + 16
        while time.monotonic() < end and len(seen) < 3:
            h = self.status().get("hack")
            if h and (not seen or seen[-1] != h):
                seen.append(h)
            time.sleep(0.3)
        self.assertEqual(seen[:3], ["ok1", "ok2", "ok1"])

    def test_config_validation(self):
        self.assertNotEqual(self.cli("set-timeout", "0").returncode, 0)
        self.assertNotEqual(self.cli("set-hack", "no such/hack").returncode, 0)
        self.assertEqual(self.cli("set-timeout", "42").returncode, 0)
        self.assertEqual(
            json.loads(self.cli("config", "get", "hack-idle-delay").stdout), 42
        )

    def test_stop_kills_process_group(self):
        self.cli("set-hack", "ok1")
        self.cli("start")
        self.assertTrue(self.wait(lambda: self.status()["running"]))
        self.cli("stop")
        time.sleep(0.5)
        out = subprocess.run(
            ["pgrep", "-f", f"{self.tmp}/bin/ok1"],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(out.stdout.strip(), "")


if __name__ == "__main__":
    unittest.main()
