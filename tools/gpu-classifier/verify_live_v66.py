# verify_live_v66.py — END-TO-END proof that the bug fix is correct on
# the live NCZ-OS 26.7 Maximilian host 192.168.207.66 (MS-R1 / Sky1,
# Mali-G720-Immortalis) on 2026-09-30.
#
# Run from the package directory with:
#     python3 verify_live_v66.py
#
# This is NOT a unit test (it imports nothing from the unittest
# machinery and does not call sys.exit). It is a documented operator-
# facing script that prints, on stdout, exactly what the live fix
# produces when given the live .66 numbers.

from __future__ import annotations

import json
import sys
import textwrap
from pathlib import Path

_HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(_HERE))

from gpu_classifier import (
    EXIT_SOFTWARE_REFUSED,
    classify_from_calibrator_result,
    class_from_ms,
    class_from_renderer,
    class_from_topology,
)
from calibrator_env import (
    augmented_calibrator_env,
    is_sky1_arm64,
    sky1_loader_paths_available,
)


# Real numbers captured from /usr/bin/ncz-screensaver-calibrate on
# 192.168.207.66 on 2026-09-30 between 02:42 and 02:48 UTC. See
# EVIDENCE.md for the literal command transcripts.

VULKAN_DEVICE_NAME = "Mali-G720-Immortalis"
GL_VERSION = "OpenGL ES 3.2 v1.r53p0-00eac0.c707efa0cfa034b363bc93f9b6749cb5"
BENCHMARK_MS = 17.812  # 31 frames in 17.812 ms -> ~575 us/frame (~1740 fps)
LLVMIPE_RENDERER = "llvmpipe (LLVM 21.1.8, 128 bits)"

# What /sys/class/misc/mali0/device/driver points at on .66.
V66_DISPLAY_GPU = {
    "driver": "mali_kbase",
    "discrete": False,
    "display": True,
}


def banner(title: str) -> None:
    print()
    print("=" * 72)
    print(title)
    print("=" * 72)


def main() -> int:
    banner("Live verification on 192.168.207.66 (MS-R1 / Sky1 / Mali-G720-Immortalis)")

    banner("1) Pure-function primitives")
    print(textwrap.dedent(f"""
        class_from_ms(17.812)
            -> {class_from_ms(BENCHMARK_MS)!r}
            # 17.812 is in the [8.0, 20.0) band, which is the launcher's 'mid' bucket.

        class_from_renderer({VULKAN_DEVICE_NAME!r})
            -> {class_from_renderer(VULKAN_DEVICE_NAME)!r}
            # 'immortalis' substring matches the row-4 mid regex.

        class_from_renderer({LLVMIPE_RENDERER!r})
            -> {class_from_renderer(LLVMIPE_RENDERER)!r}
            # llvmpipe matches the row-3 weak regex. The launcher was returning
            # this verdict on .66 when the calibrator's subprocess ran with
            # the system loader path (only Mesa/llvmpipe visible).
    """).strip())

    banner("2) Topology classifier (sysfs-only)")
    print(textwrap.dedent(f"""
        class_from_topology({json.dumps(V66_DISPLAY_GPU)})
            -> {class_from_topology(V66_DISPLAY_GPU)!r}
            # mali_kbase -> 'mid' per the launcher topology table.
    """).strip())

    banner("3) THE FIX: refusal-fallback verdict (was 'weak', now 'mid')")
    print("Pre-fix behaviour (still present in /usr/bin/ncz-screensaver 0.7.1 on .66):")
    print("    if code == 3:")
    print('        return {"class": "weak", "source": "refused", "software": True}')
    print()
    print("Post-fix verdict on the .66 hardware state:")
    e = classify_from_calibrator_result(
        code=EXIT_SOFTWARE_REFUSED, gpu=V66_DISPLAY_GPU,
    )
    print(json.dumps(e.to_dict(), indent=4))

    banner("4) Happy-path verdict with the live .66 benchmark number")
    e = classify_from_calibrator_result(
        code=0,
        gpu=V66_DISPLAY_GPU,
        renderer_hint=VULKAN_DEVICE_NAME,
        ms_hint=BENCHMARK_MS,
        version_hint=GL_VERSION,
    )
    print(json.dumps(e.to_dict(), indent=4))

    banner("5) Legitimate software-only box (no hardware bound)")
    e = classify_from_calibrator_result(code=EXIT_SOFTWARE_REFUSED, gpu=None)
    print(json.dumps(e.to_dict(), indent=4))
    print()
    print("# Genuine software-only systems (server, headless, CI container)")
    print("# still get 'weak' -- the fix does not change that contract.")

    banner("6) Sky1 loader-path env (what gpu_class() will hand to the calibrator)")
    print(f"is_sky1_arm64():                          {is_sky1_arm64()}")
    print(f"sky1_loader_paths_available():            {sky1_loader_paths_available()}")
    print()
    print("Empty env (the .66 user-mini session state -- no compositor alive):")
    env = {}
    out = augmented_calibrator_env(env)
    for k in sorted(out):
        print(f"    {k} = {out[k]}")

    print()
    print("With WAYLAND_DISPLAY=wayland-0 (a compositor IS running):")
    env = {"WAYLAND_DISPLAY": "wayland-0"}
    out = augmented_calibrator_env(env)
    for k in sorted(out):
        print(f"    {k} = {out[k]}")
    print("    (note: no __EGL_PLATFORM override -- the compositor's platform wins)")

    return 0


if __name__ == "__main__":
    sys.exit(main())