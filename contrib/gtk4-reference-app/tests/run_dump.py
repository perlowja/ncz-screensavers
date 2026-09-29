#!/usr/bin/env python3
"""Run the reference app's headless --dump; exit 77 (skip) when it cannot start here."""

import json
import subprocess
import sys

try:
    import gi  # noqa: F401
except ImportError:
    sys.exit(77)
proc = subprocess.run(
    [sys.executable, sys.argv[1], "--dump"], capture_output=True, text=True, check=False
)
if "not installed" in proc.stderr or "No module named" in proc.stderr:
    sys.exit(77)
try:
    dump = json.loads(proc.stdout)
except ValueError:
    print(proc.stdout[-300:], proc.stderr[-300:])
    sys.exit(1)
sys.exit(0 if len(dump.get("catalog", [])) >= 80 else 1)
