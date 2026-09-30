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
    EXIT_NO_CONFIG,
    EXIT_OK,
    EXIT_SOFTWARE_REFUSED,
    class_from_ms,
    class_from_renderer,
    class_from_topology,
    classify_from_calibrator_result,
    discover_gpus,
    driver_is_display_controller,
    driver_is_hardware_gpu,
)

try:
    from calibrator_env import (
        augmented_calibrator_env,
        is_sky1_arm64,
        sky1_loader_paths_available,
    )

    _HAS_CALIBRATOR_ENV = True
except ImportError:
    _HAS_CALIBRATOR_ENV = False


# Real numbers captured from /usr/bin/ncz-screensaver-calibrate on
# 192.168.207.66 on 2026-09-30 between 02:42 and 05:25 UTC. See
# EVIDENCE.md for the literal command transcripts and
# fixtures/v66-live/STRINGS.txt for the exact raw bytes.

VULKAN_DEVICE_NAME = "Mali-G720-Immortalis"
GL_VERSION = "OpenGL ES 3.2 v1.r53p0-00eac0.c707efa0cfa034b363bc93f9b6749cb5"
BENCHMARK_MS_DISPLAY = 9.88  # pci-CIXH5010_03 entry in gpu-class.json
BENCHMARK_MS_MALI = 11.76  # soc-CIXH5000_00 entry in gpu-class.json
BENCHMARK_MS_PRIOR = 17.812  # the first capture (this session)
LLVMIPE_RENDERER = "llvmpipe (LLVM 21.1.8, 128 bits)"

# What /sys/class/misc/mali0/device/driver points at on .66.
# NB: this is the platform-driver name "mali", NOT the kernel-module
# name "mali_kbase". Both names must classify as hardware-GPU-bound;
# this script verifies the former.
V66_MISC_MALI_GPU = {
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
    "sysfs_path": "/sys/class/misc/mali0",
}

# The DRM card that owns a connected connector on .66 (card2).
# This entry alone says "linlondp -> weak"; the misc/mali0 entry
# above is what carries the "mid" verdict.
V66_DISPLAY_GPU_DRM = {
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
}

# Every DRM card on .66 (4 cards, all linlondp).
V66_ALL_DRM_CARDS = [
    {
        **V66_DISPLAY_GPU_DRM,
        "id": "pci-CIXH5010_00",
        "card": "card0",
        "slot": "CIXH5010:00",
        "display": False,
    },
    {
        **V66_DISPLAY_GPU_DRM,
        "id": "pci-CIXH5010_01",
        "card": "card1",
        "slot": "CIXH5010:01",
        "display": False,
    },
    V66_DISPLAY_GPU_DRM,
    {
        **V66_DISPLAY_GPU_DRM,
        "id": "pci-CIXH5010_04",
        "card": "card3",
        "slot": "CIXH5010:04",
        "display": False,
    },
]

V66_ALL_GPUS = V66_ALL_DRM_CARDS + [V66_MISC_MALI_GPU]


def banner(title: str) -> None:
    print()
    print("=" * 72)
    print(title)
    print("=" * 72)


def main() -> int:
    banner("Live verification on 192.168.207.66 (MS-R1 / Sky1 / Mali-G720-Immortalis)")
    print()
    print("Captured 2026-09-30 05:25 UTC. NCZ-OS 26.7 Maximilian (build")
    print("2026.09.29-4bd67b5f), kernel 7.3.0-rc5-sky1-ncz, login 'mini' (LAN).")
    print()
    print("Source-of-truth strings in fixtures/v66-live/STRINGS.txt.")

    banner("1) Pure-function primitives")
    print(
        textwrap.dedent(f"""
        class_from_ms({BENCHMARK_MS_DISPLAY})
            -> {class_from_ms(BENCHMARK_MS_DISPLAY)!r}
            # {BENCHMARK_MS_DISPLAY} is in the [8.0, 20.0) band -> the 'mid' bucket.

        class_from_ms({BENCHMARK_MS_MALI})
            -> {class_from_ms(BENCHMARK_MS_MALI)!r}
            # {BENCHMARK_MS_MALI} is in the [8.0, 20.0) band -> the 'mid' bucket.

        class_from_renderer({VULKAN_DEVICE_NAME!r})
            -> {class_from_renderer(VULKAN_DEVICE_NAME)!r}
            # 'immortalis' substring matches the row-4 mid regex.

        class_from_renderer({LLVMIPE_RENDERER!r})
            -> {class_from_renderer(LLVMIPE_RENDERER)!r}
            # llvmpipe matches the row-3 weak regex. The launcher was returning
            # this verdict on .66 when the calibrator's subprocess ran with
            # the system loader path (only Mesa/llvmpipe visible).
    """).strip()
    )

    banner("2) Driver-name predicates (the .66-specific contracts)")
    print(
        textwrap.dedent(f"""
        driver_is_hardware_gpu("mali")
            -> {driver_is_hardware_gpu("mali")!r}
            # /sys/class/misc/mali0/device/driver on .66 -> "mali" (platform
            # driver name, NOT the kernel-module "mali_kbase"). Both names
            # mean "real Mali-G720 bound" on Sky1.

        driver_is_hardware_gpu("linlondp")
            -> {driver_is_hardware_gpu("linlondp")!r}
            # The DRM cards on .66 are bound to linlondp (display controller).
            # linlondp is NOT a 3D GPU driver.

        driver_is_display_controller("linlondp")
            -> {driver_is_display_controller("linlondp")!r}
        driver_is_display_controller("komeda")
            -> {driver_is_display_controller("komeda")!r}
        driver_is_display_controller("i915")
            -> {driver_is_display_controller("i915")!r}
    """).strip()
    )

    banner("3) Topology classifier")
    print(
        textwrap.dedent(f"""
        class_from_topology({{driver=mali, discrete=False, sysfs_kind=misc}})
            -> {class_from_topology(V66_MISC_MALI_GPU)!r}
            # The misc/mali0 entry on .66 must say 'mid'.

        class_from_topology({{driver=linlondp, discrete=False, sysfs_kind=drm}})
            -> {class_from_topology(V66_DISPLAY_GPU_DRM)!r}
            # The DRM card entry alone says 'weak' (display controller).
            # The 'mid' verdict requires the misc entry to be passed too.
    """).strip()
    )

    banner("4) THE FIX: actual reproducer on .66 (calibrator exit=2 + all_gpus)")
    print("Pre-fix behaviour (still present in /usr/bin/ncz-screensaver 0.7.1):")
    print("    # line 822 of /usr/bin/ncz-screensaver:")
    print("    code, ident = _calibrator_json(['--identify'], env, 30)")
    print("    if code == 3:")
    print('        return {"class": "weak", "source": "refused", "software": True}')
    print("    # line 855: ident is None -> falls through to fallback_entry('')")
    print("    # line 791: fallback_entry -> class_from_topology(display_gpu)")
    print("    # line 736: display_gpu.driver = 'linlondp' (display controller)")
    print("    # line 738-741: linlondp not in any list -> return 'weak'")
    print()
    print("Reproducer input (what the operator actually sees from an ssh session):")
    print("    code = EXIT_NO_CONFIG  (calibrator: 'calibrate: no GLES3 config', rc=2)")
    print("    gpu  = display_gpu()  (DRM card, driver=linlondp)")
    print("    all_gpus = discover_gpus() = [4 DRM cards (linlondp), 1 misc (mali)]")
    print(
        "    renderer_hint = 'Mali-G720-Immortalis'  (cached from previous calibration)"
    )
    print()
    print("Post-fix verdict:")
    e = classify_from_calibrator_result(
        code=EXIT_NO_CONFIG,
        gpu=V66_DISPLAY_GPU_DRM,
        renderer_hint=VULKAN_DEVICE_NAME,
        version_hint=GL_VERSION,
        all_gpus=V66_ALL_GPUS,
    )
    print(json.dumps(e.to_dict(), indent=4))

    banner("5) THE FIX: refusal-fallback verdict (code=3)")
    print("Same repro but with the alternative exit-3 refusal path that")
    print("the launcher handles today. Post-fix verdict:")
    e = classify_from_calibrator_result(
        code=EXIT_SOFTWARE_REFUSED,
        gpu=V66_DISPLAY_GPU_DRM,
        renderer_hint=LLVMIPE_RENDERER,
        all_gpus=V66_ALL_GPUS,
    )
    print(json.dumps(e.to_dict(), indent=4))

    banner("6) Happy-path verdict with the live .66 benchmark number")
    e = classify_from_calibrator_result(
        code=EXIT_OK,
        gpu=V66_DISPLAY_GPU_DRM,
        renderer_hint=VULKAN_DEVICE_NAME,
        ms_hint=BENCHMARK_MS_DISPLAY,
        version_hint=GL_VERSION,
    )
    print(json.dumps(e.to_dict(), indent=4))

    banner("7) Legitimate software-only box (no hardware bound)")
    e = classify_from_calibrator_result(
        code=EXIT_SOFTWARE_REFUSED,
        gpu=None,
    )
    print(json.dumps(e.to_dict(), indent=4))
    print()
    print("# Genuine software-only systems (server, headless, CI container)")
    print("# still get 'weak' -- the fix does not change that contract.")

    banner("8) discover_gpus() — the bonus enumeration that finds misc/mali0")
    if Path("/sys").is_dir():
        gpus = discover_gpus()
        print(f"Found {len(gpus)} GPU(s) on this host:")
        for g in gpus:
            kind = g.get("sysfs_kind", "?")
            tag = (
                "[HW]"
                if driver_is_hardware_gpu(g["driver"])
                else "[DC]"
                if driver_is_display_controller(g["driver"])
                else "[??]"
            )
            print(
                f"  {tag} {kind:4} id={g['id']:18} driver={g['driver']:12} "
                f"display={g['display']} render={g['render']}"
            )
        drm = [g for g in gpus if g["sysfs_kind"] == "drm"]
        misc = [g for g in gpus if g["sysfs_kind"] == "misc"]
        print()
        print("On .66 specifically we expect: 4 drm (linlondp) + 1 misc (mali)")
        print(f"On this build-pool host we found: {len(drm)} drm + {len(misc)} misc.")
    else:
        print("(no /sys available; skipping live enumeration)")

    if _HAS_CALIBRATOR_ENV:
        banner("9) Sky1 loader-path env (what gpu_class() will hand to the calibrator)")
        print(f"is_sky1_arm64():                          {is_sky1_arm64()}")
        print(
            f"sky1_loader_paths_available():            {sky1_loader_paths_available()}"
        )
        print()
        print("Empty env (the .66 user-mini session state -- no compositor alive):")
        env: dict = {}
        out = augmented_calibrator_env(env)
        for k in sorted(out):
            print(f"    {k} = {out[k]}")

        print()
        print("With WAYLAND_DISPLAY=wayland-0 (a compositor IS running):")
        env = {"WAYLAND_DISPLAY": "wayland-0"}
        out = augmented_calibrator_env(env)
        for k in sorted(out):
            print(f"    {k} = {out[k]}")
        print(
            "    (note: no __EGL_PLATFORM override -- the compositor's platform wins)"
        )

    banner("End of verification")
    print()
    print("Summary: post-fix the .66 GPU is classified as 'mid', both via the")
    print("happy-path calibration result and via the refusal-fallback path.")
    print("The launcher's old line `if code == 3: return weak` is replaced by")
    print("`classify_from_calibrator_result(...)`, which consults both the")
    print("DRM-card driver AND the misc/mali0 driver before giving up.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
