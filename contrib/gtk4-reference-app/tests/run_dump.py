#!/usr/bin/env python3
"""Run the reference app's headless --dump; exit 77 (skip) when it cannot start here."""

import json
import os
import pathlib
import subprocess
import sys
import tempfile

try:
    import gi

    gi.require_version("Gtk", "4.0")
    gi.require_version("Adw", "1")
except (ImportError, ValueError):
    sys.exit(77)
schema = (
    pathlib.Path(sys.argv[1]).resolve().parents[2]
    / "config/dev.ncz.screensaver.gschema.xml"
)
with tempfile.TemporaryDirectory() as tmp:
    (pathlib.Path(tmp) / schema.name).write_bytes(schema.read_bytes())
    if subprocess.run(["glib-compile-schemas", tmp], check=False).returncode != 0:
        sys.exit(77)
    env = dict(os.environ, GSETTINGS_SCHEMA_DIR=tmp, GSETTINGS_BACKEND="memory")
    env.setdefault(
        "NCZ_SCREENSAVER_CATALOG",
        str(schema.parents[1] / "assets/screensaver-chooser/hacks.tsv"),
    )
    proc = subprocess.run(
        [sys.executable, sys.argv[1], "--dump"],
        capture_output=True,
        text=True,
        check=False,
        env=env,
    )
try:
    dump = json.loads(proc.stdout)
except ValueError:
    print(proc.stdout[-300:], proc.stderr[-300:])
    sys.exit(1)
sys.exit(0 if len(dump.get("catalog", [])) >= 80 else 1)
