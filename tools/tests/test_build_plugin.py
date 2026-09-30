"""build-plugin.sh must fail loudly: non-zero and no output file on a broken source."""

import os
import pathlib
import shutil
import subprocess

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SDK = pathlib.Path(os.environ.get("NCZ_TEST_SDK", pathlib.Path.home() / "ss-sdk"))
pytestmark = pytest.mark.skipif(
    not (SDK / "lib/pkgconfig/singularity-1.0.pc").exists()
    or not shutil.which("valac"),
    reason="needs valac and a Singularity SDK (NCZ_TEST_SDK)",
)


def run(src, out):
    env = dict(os.environ, NCZ_PLUGIN_SRC=str(src))
    return subprocess.run(
        ["sh", str(ROOT / "tools/build-plugin.sh"), str(SDK), str(out)],
        capture_output=True,
        text=True,
        env=env,
        check=False,
    )


def test_broken_source_fails_loudly(tmp_path):
    src = tmp_path / "plugin"
    shutil.copytree(ROOT / "plugin", src)
    with (src / "screensaver/screensaver.vala").open("a") as f:
        f.write("\nthis is not vala;\n")
    out = tmp_path / "out"
    proc = run(src, out)
    assert proc.returncode != 0
    assert "error" in proc.stderr.lower()
    assert not (out / "libscreensaver.so").exists()


def test_good_source_builds(tmp_path):
    out = tmp_path / "out"
    proc = run(ROOT / "plugin", out)
    assert proc.returncode == 0, proc.stderr
    assert (out / "libscreensaver.so").is_file()
