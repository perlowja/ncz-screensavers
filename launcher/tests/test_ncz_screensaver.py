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
from unittest import mock

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


def make_sysfs(root, cards):
    """cards: list of (driver, connected, vram_bytes or None)."""
    for i, (driver, connected, vram) in enumerate(cards):
        card = Path(root) / "class" / "drm" / f"card{i}"
        (card / "device").mkdir(parents=True)
        drv = Path(root) / "drivers" / driver
        drv.mkdir(parents=True, exist_ok=True)
        (card / "device" / "driver").symlink_to(drv)
        if vram is not None:
            (card / "device" / "mem_info_vram_total").write_text(str(vram))
        conn = Path(root) / "class" / "drm" / f"card{i}-eDP-1"
        conn.mkdir(parents=True)
        (conn / "status").write_text("connected\n" if connected else "disconnected\n")


SCHEMA_ROWS = [
    "palette\tenum\tstylized\t\t\tstylized,kipthorne,faithful,slingshot,singularity\tPalette\tColors\tGeneral\tNCZ_BLACKHOLE_PALETTE",
    "spin\tfloat\t0.5\t0\t1\t\tSpin\tBlack hole spin\tPhysics\t",
    "duration\tint\t0\t0\t3600\t\tDuration\tSeconds\tGeneral\t",
    "beaming\tbool\ttrue\t\t\t\tBeaming\tDoppler beaming\tPhysics\t",
    "title\tstring\t\t\t\t\tTitle\tText\tGeneral\t",
]


class HackOptionTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="ncz-opt-")
        Path(self.tmp, "blackhole.tsv").write_text(
            "\n".join(["# comment", *SCHEMA_ROWS]) + "\n"
        )
        os.environ["NCZ_SCREENSAVER_OPTIONS"] = self.tmp

    def tearDown(self):
        os.environ.pop("NCZ_SCREENSAVER_OPTIONS", None)
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_schema_and_derived_env_names(self):
        rows = {r["name"]: r for r in ns.load_option_schema("blackhole_gles3")}
        self.assertEqual(rows["palette"]["env"], "NCZ_BLACKHOLE_PALETTE")
        self.assertEqual(rows["spin"]["env"], "NCZ_BLACKHOLE_SPIN")
        self.assertEqual(
            rows["palette"]["choices"], ["stylized", "kipthorne", "faithful", "slingshot", "singularity"]
        )
        self.assertEqual(ns.load_option_schema("voronoi_gles3"), [])

    def test_normalize(self):
        rows = {r["name"]: r for r in ns.load_option_schema("blackhole_gles3")}
        self.assertEqual(ns.normalize_option(rows["beaming"], "Yes"), "true")
        self.assertEqual(ns.normalize_option(rows["duration"], "90"), "90")
        self.assertEqual(ns.normalize_option(rows["spin"], "0.25"), "0.25")
        for name, bad in (
            ("palette", "neon"),
            ("spin", "1.5"),
            ("duration", "-1"),
            ("beaming", "maybe"),
            ("duration", "x"),
        ):
            with self.assertRaises(ValueError):
                ns.normalize_option(rows[name], bad)

    def test_env_passes_only_valid_schema_options(self):
        settings = {
            "hack-options": {
                "blackhole_gles3": {"palette": "slingshot", "spin": "9", "bogus": "1"}
            }
        }
        env = ns.hack_option_env("blackhole_gles3", settings)
        self.assertEqual(env, {"NCZ_BLACKHOLE_PALETTE": "slingshot"})
        self.assertEqual(ns.hack_option_env("voronoi_gles3", settings), {})

    def test_overrides_beat_stored_and_legacy_color_fills_palette(self):
        settings = {
            "hack-options": {"blackhole_gles3": {"palette": "slingshot"}},
            "blackhole-color-mode": "kipthorne",
        }
        env = ns.hack_option_env(
            "blackhole_gles3", settings, {"palette": "singularity"}
        )
        self.assertEqual(env["NCZ_BLACKHOLE_PALETTE"], "singularity")
        env = ns.hack_option_env(
            "blackhole_gles3", {"blackhole-color-mode": "kipthorne"}
        )
        self.assertEqual(env["NCZ_BLACKHOLE_PALETTE"], "kipthorne")

    def test_child_env_carries_options_only_for_that_process(self):
        settings = {
            "hack-options": {"blackhole_gles3": {"spin": "0.75"}},
            "gpu-offload": "off",
        }
        env = ns.build_child_env(
            "blackhole_gles3", settings, {"XDG_RUNTIME_DIR": "/x"}, "/nonexistent"
        )
        self.assertEqual(env["NCZ_BLACKHOLE_SPIN"], "0.75")
        self.assertNotIn("NCZ_BLACKHOLE_SPIN", os.environ)

    def test_gvariant_dict_roundtrip(self):
        value = {"blackhole_gles3": {"palette": "slingshot", "spin": "0.5"}}
        self.assertEqual(ns.parse_gvariant(ns.gvariant_text(value)), value)
        self.assertEqual(ns.parse_gvariant(ns.gvariant_text({})), {})


class GpuPolicyTests(unittest.TestCase):
    def topo(self, cards):
        with tempfile.TemporaryDirectory() as tmp:
            make_sysfs(tmp, cards)
            os.environ["NCZ_SCREENSAVER_SYSFS"] = tmp
            ns.gpu_topology.cache_clear()
            try:
                return ns.gpu_topology()
            finally:
                del os.environ["NCZ_SCREENSAVER_SYSFS"]
                ns.gpu_topology.cache_clear()

    def test_intel_display_with_nvidia_is_offload_capable(self):
        t = self.topo([("i915", True, None), ("nvidia", False, None)])
        self.assertEqual(t, {"display_class": "integrated", "nvidia_offload": True})

    def test_nvidia_display_needs_no_offload(self):
        t = self.topo([("nvidia", True, None)])
        self.assertEqual(t, {"display_class": "discrete", "nvidia_offload": False})

    def test_no_nvidia_no_offload(self):
        self.assertFalse(
            self.topo([("i915", True, None), ("amdgpu", False, 8 * 1024**3)])[
                "nvidia_offload"
            ]
        )

    def test_amd_vram_decides_class(self):
        self.assertEqual(
            self.topo([("amdgpu", True, 8 * 1024**3)])["display_class"], "discrete"
        )
        self.assertEqual(
            self.topo([("amdgpu", True, 512 * 1024**2)])["display_class"], "integrated"
        )

    def test_offload_only_for_discrete_tier_in_auto(self):
        ns.gpu_topology.cache_clear()
        with mock.patch.object(
            ns,
            "gpu_topology",
            return_value={"display_class": "integrated", "nvidia_offload": True},
        ):
            tiers = {"a": "igpu", "b": "discrete"}
            self.assertFalse(ns.offload_for("a", {"gpu-offload": "auto"}, tiers))
            self.assertTrue(ns.offload_for("b", {"gpu-offload": "auto"}, tiers))
            self.assertTrue(ns.offload_for("a", {"gpu-offload": "prime"}, tiers))
            self.assertFalse(ns.offload_for("b", {"gpu-offload": "off"}, tiers))
        with mock.patch.object(
            ns,
            "gpu_topology",
            return_value={"display_class": "integrated", "nvidia_offload": False},
        ):
            self.assertFalse(
                ns.offload_for("b", {"gpu-offload": "auto"}, {"b": "discrete"})
            )

    def test_pool_filter(self):
        tiers = {"a": "igpu", "b": "discrete"}
        with mock.patch.object(ns, "load_tiers", return_value=tiers):
            with mock.patch.object(
                ns,
                "gpu_topology",
                return_value={"display_class": "integrated", "nvidia_offload": False},
            ):
                self.assertEqual(
                    ns.filter_pool(["a", "b"], {"pool-gpu-class": "auto"}), ["a"]
                )
                self.assertEqual(
                    ns.filter_pool(["a", "b"], {"pool-gpu-class": "all"}), ["a", "b"]
                )
                self.assertEqual(
                    ns.filter_pool(["b"], {"pool-gpu-class": "auto"}), ["b"]
                )
            with mock.patch.object(
                ns,
                "gpu_topology",
                return_value={"display_class": "discrete", "nvidia_offload": False},
            ):
                self.assertEqual(
                    ns.filter_pool(["a", "b"], {"pool-gpu-class": "auto"}), ["a", "b"]
                )
            with mock.patch.object(
                ns,
                "gpu_topology",
                return_value={"display_class": "integrated", "nvidia_offload": True},
            ):
                self.assertEqual(
                    ns.filter_pool(
                        ["a", "b"], {"pool-gpu-class": "auto", "gpu-offload": "auto"}
                    ),
                    ["a", "b"],
                )
                self.assertEqual(
                    ns.filter_pool(
                        ["a", "b"], {"pool-gpu-class": "auto", "gpu-offload": "off"}
                    ),
                    ["a"],
                )
        with mock.patch.object(ns, "load_tiers", return_value={}):
            self.assertEqual(
                ns.filter_pool(["a", "b"], {"pool-gpu-class": "igpu-only"}), ["a", "b"]
            )


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

    def test_option_commands(self):
        opts = Path(self.tmp) / "options"
        opts.mkdir()
        (opts / "ok1.tsv").write_text("\n".join(SCHEMA_ROWS) + "\n")
        self.env["NCZ_SCREENSAVER_OPTIONS"] = str(opts)
        self.assertEqual(
            self.cli("set-option", "ok1", "palette", "slingshot").returncode, 0
        )
        self.assertEqual(
            self.cli("get-option", "ok1", "palette").stdout.strip(), "slingshot"
        )
        self.assertNotEqual(
            self.cli("set-option", "ok1", "palette", "neon").returncode, 0
        )
        self.assertNotEqual(self.cli("set-option", "ok1", "nosuch", "1").returncode, 0)
        rows = json.loads(self.cli("list-options", "ok1", "--json").stdout)
        self.assertEqual({r["name"]: r["value"] for r in rows}["palette"], "slingshot")
        self.assertEqual(self.cli("reset-options", "ok1").returncode, 0)
        self.assertEqual(
            self.cli("get-option", "ok1", "palette").stdout.strip(), "stylized"
        )

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
