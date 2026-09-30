"""Fixture-based tests for GPU class detection, offload planning and pool selection.

Run: python3 -m pytest tools/tests/test_gpu_class.py
Every test builds a fake sysfs tree, a fake calibrator and a fake tiers.tsv under a
temporary directory; nothing touches the real GPU or the real cache.
"""

import importlib.machinery
import importlib.util
import json
import os
import pathlib
import stat
import textwrap

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[2]


def load_launcher():
    loader = importlib.machinery.SourceFileLoader(
        "ncz_launcher", str(ROOT / "launcher/ncz-screensaver")
    )
    spec = importlib.util.spec_from_loader("ncz_launcher", loader)
    mod = importlib.util.module_from_spec(spec)
    loader.exec_module(mod)
    return mod


def add_gpu(
    sysfs,
    card,
    vendor,
    device,
    driver,
    slot,
    connected=False,
    boot_vga=False,
    vram=None,
):
    dev = sysfs / "class/drm" / card / "device"
    dev.mkdir(parents=True)
    (dev / "vendor").write_text(vendor + "\n")
    (dev / "device").write_text(device + "\n")
    (dev / "uevent").write_text(f"PCI_SLOT_NAME={slot}\n")
    (dev / "boot_vga").write_text("1\n" if boot_vga else "0\n")
    if vram:
        (dev / "mem_info_vram_total").write_text(f"{vram}\n")
    target = sysfs / "drivers" / driver
    target.mkdir(parents=True, exist_ok=True)
    os.symlink(target, dev / "driver")
    if connected:
        conn = sysfs / "class/drm" / f"{card}-eDP-1"
        conn.mkdir()
        (conn / "status").write_text("connected\n")


def add_power(sysfs, ac, battery=True):
    base = sysfs / "class/power_supply"
    (base / "AC").mkdir(parents=True)
    (base / "AC/type").write_text("Mains\n")
    (base / "AC/online").write_text("1\n" if ac else "0\n")
    if battery:
        (base / "BAT0").mkdir()
        (base / "BAT0/type").write_text("Battery\n")


GIB = 1024**3
LAYOUTS = {
    # PEGASUS: Intel iGPU owns the panel, NVIDIA dGPU is the offload target.
    "nvidia+intel": lambda s: (
        add_gpu(
            s,
            "card0",
            "0x8086",
            "0x9bca",
            "i915",
            "0000:00:02.0",
            connected=True,
            boot_vga=True,
        ),
        add_gpu(s, "card1", "0x10de", "0x1f08", "nvidia", "0000:01:00.0"),
    ),
    # CHIMERA and MEDUSA: the AMD dGPU owns the panel, the Intel iGPU is idle.
    "amd-dgpu-display+intel-idle": lambda s: (
        add_gpu(s, "card0", "0x8086", "0x3e9b", "i915", "0000:00:02.0"),
        add_gpu(
            s,
            "card1",
            "0x1002",
            "0x7340",
            "amdgpu",
            "0000:03:00.0",
            connected=True,
            boot_vga=True,
            vram=8 * GIB,
        ),
    ),
    # AMD APU on the panel, AMD dGPU idle.
    "amd-apu+amd-dgpu": lambda s: (
        add_gpu(
            s,
            "card0",
            "0x1002",
            "0x1638",
            "amdgpu",
            "0000:06:00.0",
            connected=True,
            boot_vga=True,
            vram=512 * 1024**2,
        ),
        add_gpu(
            s, "card1", "0x1002", "0x73df", "amdgpu", "0000:03:00.0", vram=12 * GIB
        ),
    ),
    "single-soc": lambda s: add_gpu(
        s, "card0", "0x0000", "0x0000", "mali_kbase", "0000:00:00.0", connected=True
    ),
    # MUX switch in dGPU-only mode: the NVIDIA GPU drives the panel, the iGPU is off.
    "mux-dgpu-only": lambda s: add_gpu(
        s,
        "card0",
        "0x10de",
        "0x2520",
        "nvidia",
        "0000:01:00.0",
        connected=True,
        boot_vga=True,
    ),
}

FAKE_CALIBRATOR = textwrap.dedent("""\
    #!/bin/sh
    # fake ncz-screensaver-calibrate: FAKE_MS, FAKE_RENDERER, FAKE_VERSION, FAKE_EXIT drive it
    [ -n "$FAKE_EXIT" ] && [ "$FAKE_EXIT" != 0 ] && exit "$FAKE_EXIT"
    case "$DRI_PRIME$__NV_PRIME_RENDER_OFFLOAD" in
        pci-0000_00_02_0) r="Mesa Intel(R) UHD Graphics 630 (CFL GT2)"; ms=46.5 ;;
        pci-0000_03_00_0) r="AMD Radeon Graphics (radeonsi, navi14)"; ms=5.5 ;;
        1) r="NVIDIA GeForce RTX 2060/PCIe/SSE2"; ms=2.7 ;;
        *) r="${FAKE_RENDERER:-unknown}"; ms="${FAKE_MS:-9.9}" ;;
    esac
    v="${FAKE_VERSION:-OpenGL ES 3.2 Mesa 26.0}"
    if [ "$1" = "--identify" ]; then
        printf '{"renderer":"%s","version":"%s","platform":"fake","identify":true}\\n' "$r" "$v"
    else
        printf '{"renderer":"%s","version":"%s","platform":"fake","timer":"gpu","frames":20,"ms":%s}\\n' "$r" "$v" "$ms"
    fi
    """)

TIERS = textwrap.dedent("""\
    # id\tmin\tuhd630\tuhd630_scaled\tmali\tnavi14\trtx2060\tmeasured
    hyprsaver_light_gles3\tweak\t60.0/16.7\t60.0/16.7\t60.0/16.7\t-\t-\t2026-09-29 test
    light_gles3\tweak\t60.0/16.7\t60.0/16.7\t60.0/16.7\t-\t-\t2026-09-29 test
    medium_gles3\tmid\t14.0/80.0\t28.0/45.0\t48.0/21.0\t-\t-\t2026-09-29 test
    heavy_gles3\tstrong\t9.0/120.0\t12.0/95.0\t20.0/60.0\t-\t-\t2026-09-29 test
    """)


@pytest.fixture
def env(tmp_path, monkeypatch):
    sysfs = tmp_path / "sys"
    sysfs.mkdir()
    cal = tmp_path / "calibrate"
    cal.write_text(FAKE_CALIBRATOR)
    cal.chmod(cal.stat().st_mode | stat.S_IXUSR)
    tiers = tmp_path / "tiers.tsv"
    tiers.write_text(TIERS.replace("\\t", "\t"))
    monkeypatch.setenv("NCZ_SCREENSAVER_SYSFS", str(sysfs))
    monkeypatch.setenv("NCZ_SCREENSAVER_CALIBRATE_BIN", str(cal))
    monkeypatch.setenv("NCZ_SCREENSAVER_TIERS", str(tiers))
    monkeypatch.setenv("NCZ_SCREENSAVER_BROKEN", str(tmp_path / "none.tsv"))
    monkeypatch.setenv("NCZ_SCREENSAVER_SWITCHEROOCTL", str(tmp_path / "absent"))
    monkeypatch.setenv("XDG_CACHE_HOME", str(tmp_path / "cache"))
    monkeypatch.setenv("NCZ_SCREENSAVER_PROC_ROOT", str(tmp_path / "proc"))
    (tmp_path / "proc").mkdir()
    (tmp_path / "cache").mkdir()
    mod = load_launcher()
    mod.tmp = tmp_path
    mod.sysfs = sysfs
    return mod


def layout(mod, name, ac=True):
    LAYOUTS[name](mod.sysfs)
    add_power(mod.sysfs, ac)
    for fn in (mod.list_gpus, mod.switcheroo_envs, mod.gpu_topology):
        fn.cache_clear()


def seed_cache(mod, entries):
    path = mod.class_cache_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps({"entries": entries}))


def seed_display(mod, cls):
    seed_cache(mod, {mod.display_gpu()["id"]: {"class": cls}})


S = {"gpu-offload": "auto", "pool-gpu-class": "auto"}


# ---- class detection ------------------------------------------------------------------
@pytest.mark.parametrize(
    "renderer,expected",
    [
        ("Mesa Intel(R) UHD Graphics 630 (CFL GT2)", "weak"),
        ("Mesa Intel(R) UHD Graphics (CML GT2)", "weak"),
        ("Mali-G720-Immortalis", "mid"),
        ("Mali-G52 MC2", "weak"),
        ("V3D 7.1.7", "weak"),
        ("AMD Radeon Graphics (radeonsi, navi14, ACO, DRM 3.64)", "strong"),
        ("NVIDIA GeForce RTX 2060/PCIe/SSE2", "strong"),
        ("AMD Radeon Graphics (radeonsi, renoir, LLVM 17)", "mid"),
        ("Some Future Chip 9000", None),
    ],
)
def test_renderer_table(env, renderer, expected):
    assert env.class_from_renderer(renderer) == expected


# Strings captured on 2026-09-29 from MS-R1 / cixmini (192.168.207.66, Sky1,
# Mali-G720-Immortalis). Both the GLES renderer string returned by the
# calibrator and the Vulkan deviceName/driverName returned by vulkaninfo, plus
# every plausible variant ARM has shipped for the Valhall/Immortalis family
# we expect to see on this SoC family going forward.
SKY1_RENDERERS = [
    "Mali-G720-Immortalis",
    "Mali-G720 MC7",
    "Mali-G715-Immortalis",
    "Mali-G715 MC7",
    "Mali-G710 MC7",
    "Mali-G610 MC4",
    "Mali-G610",
    # Vulkan's driverName/driverInfo path also returns the bare "Mali-G720" or
    # the form with the "Immortalis" suffix; both are mid-tier.
    "Mali-G720",
]


@pytest.mark.parametrize("renderer", SKY1_RENDERERS)
def test_sky1_mali_g720_immortalis_is_mid_not_weak(env, renderer):
    """Regression: Mali-G720-Immortalis and its Immortalis-class siblings must
    classify as 'mid', not 'weak'. The /sys/class/drm/card* entries on Sky1 are
    linlondp display controllers (no GPU); the actual GPU is a separate
    /sys/devices/platform/CIXH5000:00/misc/mali0 character device driven by
    the 'mali' platform bus driver (a CIX-specific glue around the upstream
    mali_kbase, loaded because panthor is blacklisted on this kernel).
    """
    assert env.class_from_renderer(renderer) == "mid"


# The previous weak pattern was "mali-g[35]\d\b", which under-classified
# every Valhall / Immortalis GPU (Mali-G57, G610, G615, G620, G710, G715,
# G720) as weak. The tightened weak pattern is "mali-g(?:31|52)\b"; the
# mid pattern was widened to spell out the same Valhall / Immortalis set.
@pytest.mark.parametrize(
    "renderer,expected",
    [
        # Genuinely weak Mali parts (Bifrost low-end).
        ("Mali-G31", "weak"),
        ("Mali-G31 MC2", "weak"),
        ("Mali-G52", "weak"),
        ("Mali-G52 MC2", "weak"),
        # Valhall / Immortalis parts: mid, never weak.
        ("Mali-G57", "mid"),
        ("Mali-G610", "mid"),
        ("Mali-G610 MC4", "mid"),
        ("Mali-G615", "mid"),
        ("Mali-G620", "mid"),
        ("Mali-G710", "mid"),
        ("Mali-G715", "mid"),
        ("Mali-G720", "mid"),
        ("Mali-G720-Immortalis", "mid"),
        ("Mali-G720 MC7", "mid"),
        # Older T-series and Mali-400 stay weak (the launcher never saw one
        # on NCZ-OS, but the table needs to keep the existing promise).
        ("Mali-T720", "weak"),
        ("Mali-T880", "weak"),
        ("Mali-400 MP2", "weak"),
    ],
)
def test_mali_weak_pattern_does_not_swallow_valhall(env, renderer, expected):
    """The Mali weak pattern is narrowed to G31/G52 only; G57 and the
    G610/G615/G620/G710/G715/G720 / Immortalis family stay mid. The mid
    pattern is widened to spell them out so a renderer string like
    'Mali-G57' never falls through to weak.
    """
    assert env.class_from_renderer(renderer) == expected


@pytest.mark.parametrize(
    "driver,expected",
    [
        # The real driver on .66 is the platform-bus 'mali' (CIX glue for
        # mali_kbase). It must classify as mid, not weak: that's how a Mali
        # GPU ends up classified when only the sysfs topology is available
        # (no cache, no calibrator, the launcher fell back to the table).
        ("mali", "mid"),
        # The upstream driver names used on other platforms.
        ("mali_kbase", "mid"),
        ("panthor", "mid"),
        ("panfrost", "mid"),
        # A pure display controller like linlondp (no 3D pipeline of its own)
        # is not a GPU and should not be in this list at all; we filter it
        # out earlier in list_gpus() instead. class_from_topology only ever
        # sees a GPU that survived list_gpus().
        ("i915", "weak"),
        ("xe", "weak"),
        ("amdgpu", "mid"),
    ],
)
def test_sky1_mali_platform_driver_is_mid(env, driver, expected):
    """Regression: the 'mali' platform driver on Sky1 (CIXH5000:00) is the
    same hardware as upstream mali_kbase. The fallback topology classifier
    must treat it as mid so a GPU system is never demoted to weak.
    """
    gpu = {
        "vendor": "other",
        "driver": driver,
        "discrete": False,
        "render": True,
        "id": "soc-mali",
        "display": True,
        "card": "card0",
        "slot": "CIXH5000:00",
        "device": "",
        "boot_vga": False,
    }
    assert env.class_from_topology(gpu) == expected


def test_sky1_linlondp_display_controllers_are_filtered_from_gpus(env):
    """Regression for 192.168.207.66: Sky1 exposes four DRM cards driven by
    the 'linlondp' display controller, none of which is a 3D GPU. The actual
    GPU is the /sys/devices/platform/CIXH5000:00/misc/mali0 character device
    driven by the 'mali' platform bus driver. list_gpus() must not list the
    linlondp cards as GPUs (which previously forced them to 'weak' via
    class_from_topology) — they should be filtered out and the GPU entry
    should come from the platform-bus GPU device.
    """
    # Four linlondp display controllers with no vendor/device, like on .66.
    for i, slot in enumerate(
        ("CIXH5010:00", "CIXH5010:01", "CIXH5010:02", "CIXH5010:03")
    ):
        add_gpu(
            env.sysfs,
            f"card{i}",
            "0x0000",
            "0x0000",
            "linlondp",
            slot,
            connected=(i == 3),
        )
        # The cards do have render nodes (writeback) on .66.
        (env.sysfs / f"class/drm/card{i}/device/drm").mkdir(parents=True, exist_ok=True)
        (env.sysfs / f"class/drm/card{i}/device/drm/renderD128").touch()
    # The actual GPU is a /sys/class/misc/mali0 character device, parent
    # platform device CIXH5000:00, driven by the 'mali' platform bus driver.
    misc = env.sysfs / "class/misc/mali0"
    misc.mkdir(parents=True)
    (misc / "dev").write_text("10:262\n")
    (misc / "uevent").write_text("MAJOR=10\nMINOR=262\nDEVNAME=mali0\nDEVMODE=0666\n")
    gpu_dev = env.sysfs / "devices/platform/CIXH5000:00"
    gpu_dev.mkdir(parents=True)
    (gpu_dev / "modalias").write_text("acpi:CIXH5000:\n")
    (gpu_dev / "uevent").write_text("DRIVER=mali\nMODALIAS=acpi:CIXH5000:\n")
    drv = env.sysfs / "bus/platform/drivers/mali"
    drv.mkdir(parents=True)
    os.symlink(drv, gpu_dev / "driver")
    # Mirror /sys/class/misc/mali0/device -> /sys/devices/platform/.../misc/mali0
    # the kernel exposes for every registered misc device.
    target = gpu_dev / "misc/mali0"
    target.mkdir(parents=True)
    os.symlink(target, misc / "device")
    add_power(env.sysfs, True)
    for fn in (env.list_gpus, env.gpu_topology):
        fn.cache_clear()
    gpus = env.list_gpus()
    # Exactly one GPU, the Mali; no linlondp display controller.
    assert len(gpus) == 1
    g = gpus[0]
    assert g["driver"] == "mali"
    assert g["display"] is True
    assert g["id"] == "soc-CIXH5000_00"
    # The topology reports the same SoC as the display.
    assert env.gpu_topology()["display_class"] == "integrated"


def test_sky1_mali_gpu_is_classified_mid_end_to_end(env, monkeypatch):
    """End-to-end regression for 192.168.207.66: with the Sky1 sysfs layout,
    the launcher's GPU class detection returns 'mid' (the calibrated class
    on the real hardware) — never 'weak'. This is the central guarantee of
    the operator's policy: never demote a real GPU to weak.
    """
    for i, slot in enumerate(
        ("CIXH5010:00", "CIXH5010:01", "CIXH5010:02", "CIXH5010:03")
    ):
        add_gpu(
            env.sysfs,
            f"card{i}",
            "0x0000",
            "0x0000",
            "linlondp",
            slot,
            connected=(i == 3),
        )
        (env.sysfs / f"class/drm/card{i}/device/drm").mkdir(parents=True, exist_ok=True)
        (env.sysfs / f"class/drm/card{i}/device/drm/renderD128").touch()
    misc = env.sysfs / "class/misc/mali0"
    misc.mkdir(parents=True)
    (misc / "dev").write_text("10:262\n")
    gpu_dev = env.sysfs / "devices/platform/CIXH5000:00"
    gpu_dev.mkdir(parents=True)
    (gpu_dev / "uevent").write_text("DRIVER=mali\n")
    drv = env.sysfs / "bus/platform/drivers/mali"
    drv.mkdir(parents=True)
    os.symlink(drv, gpu_dev / "driver")
    target = gpu_dev / "misc/mali0"
    target.mkdir(parents=True)
    os.symlink(target, misc / "device")
    add_power(env.sysfs, True)
    for fn in (env.list_gpus, env.gpu_topology):
        fn.cache_clear()

    monkeypatch.setenv("FAKE_RENDERER", "Mali-G720-Immortalis")
    monkeypatch.setenv(
        "FAKE_VERSION",
        "OpenGL ES 3.2 v1.r53p0-00eac0.c707efa0c707efa0cfa034b363bc93f9b6749cb5",
    )
    monkeypatch.setenv("FAKE_MS", "9.88")
    e = env.gpu_class(S, None)
    assert e["class"] == "mid"
    assert e["ms"] == 9.88
    assert e["renderer"] == "Mali-G720-Immortalis"
    # And in the gpus listing: every entry is mid, none is weak.
    for g in env.list_gpus():
        cls = env.gpu_class(S, g, run=False)["class"]
        assert cls == "mid", f"GPU {g['id']} classified {cls} not mid"


@pytest.mark.parametrize(
    "ms,expected",
    [
        (46.5, "weak"),
        (20.0, "weak"),
        (19.9, "mid"),
        (9.9, "mid"),
        (8.0, "mid"),
        (5.5, "strong"),
        (2.7, "strong"),
    ],
)
def test_class_from_score(env, ms, expected):
    assert env.class_from_ms(ms) == expected


def test_calibration_scores_reference_gpus_and_caches(env, monkeypatch):
    layout(env, "nvidia+intel")
    disp, nv = env.list_gpus()
    monkeypatch.setenv("FAKE_MS", "46.5")
    monkeypatch.setenv("FAKE_RENDERER", "Mesa Intel(R) UHD Graphics 630 (CFL GT2)")
    a = env.gpu_class(S, disp)
    b = env.gpu_class(S, nv)
    assert (a["class"], a["ms"], a["source"]) == ("weak", 46.5, "calibration")
    assert (b["class"], b["ms"]) == ("strong", 2.7)
    cache = env.read_class_cache()
    assert set(cache) == {disp["id"], nv["id"]}
    # A second call with an unchanged renderer and version reuses the cache entry.
    monkeypatch.setenv("FAKE_EXIT", "2")
    assert env.gpu_class(S, disp)["ms"] == 46.5


def test_unknown_chip_is_classified_by_score_alone(env, monkeypatch):
    layout(env, "single-soc")
    monkeypatch.setenv("FAKE_RENDERER", "Some Future Chip 9000")
    monkeypatch.setenv("FAKE_MS", "12.5")
    e = env.gpu_class(S, None)
    assert (e["class"], e["source"]) == ("mid", "calibration")


def test_cache_refreshes_when_driver_version_changes(env, monkeypatch):
    layout(env, "single-soc")
    monkeypatch.setenv("FAKE_MS", "9.9")
    assert env.gpu_class(S, None)["class"] == "mid"
    monkeypatch.setenv("FAKE_MS", "3.0")
    assert env.gpu_class(S, None)["class"] == "mid"  # same renderer and version: cached
    monkeypatch.setenv("FAKE_VERSION", "OpenGL ES 3.2 Mesa 26.1")
    assert env.gpu_class(S, None)["class"] == "strong"  # version changed: recalibrated


def test_cache_refreshes_when_renderer_changes(env, monkeypatch):
    layout(env, "single-soc")
    monkeypatch.setenv("FAKE_RENDERER", "Chip A")
    monkeypatch.setenv("FAKE_MS", "30")
    assert env.gpu_class(S, None)["class"] == "weak"
    monkeypatch.setenv("FAKE_RENDERER", "Chip B")
    monkeypatch.setenv("FAKE_MS", "6")
    assert env.gpu_class(S, None)["class"] == "strong"


def test_software_renderer_is_refused_never_benchmarked(env, monkeypatch):
    layout(env, "single-soc")
    monkeypatch.setenv("FAKE_EXIT", "3")
    e = env.gpu_class(S, None)
    assert e.get("software") is True
    assert env.read_class_cache() == {}


def test_missing_calibrator_falls_back_to_table(env, monkeypatch):
    layout(env, "nvidia+intel")
    monkeypatch.setenv("NCZ_SCREENSAVER_CALIBRATE_BIN", str(env.tmp / "missing"))
    disp, nv = env.list_gpus()
    assert env.gpu_class(S, disp)["source"] == "table"
    assert env.gpu_class(S, disp)["class"] == "weak"  # i915 -> weak
    assert env.gpu_class(S, nv)["class"] == "strong"  # discrete -> strong


def test_calibrate_command_refuses_while_a_hack_runs(env, monkeypatch):
    layout(env, "single-soc")
    monkeypatch.setattr(env, "read_state", lambda: {"pid": 1})
    monkeypatch.setattr(env, "load_settings", lambda: (dict(env.DEFAULTS), True))
    args = type("A", (), {"force": False, "json": True, "copy_ms": None})()
    assert env.cmd_calibrate(args) == 3
    assert env.read_class_cache() == {}


# ---- GPU roles, offload environment ---------------------------------------------------------
def test_pegasus_layout_display_intel_offload_nvidia(env):
    layout(env, "nvidia+intel")
    gpus = env.list_gpus()
    assert [g["vendor"] for g in gpus] == ["intel", "nvidia"]
    assert gpus[0]["display"] and not gpus[1]["display"]
    e = env.gpu_env(gpus[1])
    assert e["__NV_PRIME_RENDER_OFFLOAD"] == "1"
    assert e["__GLX_VENDOR_LIBRARY_NAME"] == "nvidia"
    assert e["__VK_LAYER_NV_optimus"] == "NVIDIA_only"
    assert "__EGL_VENDOR_LIBRARY_FILENAMES" not in e
    assert env.gpu_env(gpus[0]) == {}


def test_chimera_layout_amd_owns_panel_intel_idle(env):
    layout(env, "amd-dgpu-display+intel-idle")
    gpus = env.list_gpus()
    assert gpus[0]["vendor"] == "amd" and gpus[0]["display"] and gpus[0]["discrete"]
    intel = gpus[1]
    e = env.gpu_env(intel)
    assert e["DRI_PRIME"] == "pci-0000_00_02_0"
    assert e["MESA_VK_DEVICE_SELECT"] == "8086:3e9b"
    assert "__NV_PRIME_RENDER_OFFLOAD" not in e


def test_amd_apu_and_dgpu_uses_mesa_prime(env):
    layout(env, "amd-apu+amd-dgpu")
    gpus = env.list_gpus()
    assert not gpus[0]["discrete"] and gpus[1]["discrete"]
    assert env.gpu_env(gpus[1])["DRI_PRIME"] == "pci-0000_03_00_0"


def test_single_gpu_has_no_targets(env):
    layout(env, "single-soc")
    assert env.offload_targets(S) == []
    assert env.plan_for("heavy_gles3", S)["offload"] is False


def test_mux_dgpu_only_display_is_nvidia_and_never_offloads(env):
    layout(env, "mux-dgpu-only")
    assert env.display_gpu()["vendor"] == "nvidia"
    assert env.offload_targets(S) == []
    assert env.class_from_topology() == "strong"


def test_switcherooctl_environment_is_used_and_egl_vendor_is_dropped(env):
    layout(env, "nvidia+intel")
    fake = env.tmp / "switcherooctl"
    fake.write_text(
        "#!/bin/sh\ncat <<'EOF'\nDevice: 0\n  Name: Intel\n  Default: yes\n  Environment: DRI_PRIME=pci-0000_00_02_0\n\n"
        "Device: 1\n  Name: NVIDIA\n  Default: no\n  Environment: __GLX_VENDOR_LIBRARY_NAME=nvidia __NV_PRIME_RENDER_OFFLOAD=1 "
        "__VK_LAYER_NV_optimus=NVIDIA_only __EGL_VENDOR_LIBRARY_FILENAMES=/x/10_nvidia.json\nEOF\n"
    )
    fake.chmod(0o755)
    os.environ["NCZ_SCREENSAVER_SWITCHEROOCTL"] = str(fake)
    try:
        env.switcheroo_envs.cache_clear()
        assert "nvidia" in env.switcheroo_envs()
        e = env.gpu_env(env.list_gpus()[1])
        assert e["__NV_PRIME_RENDER_OFFLOAD"] == "1"
        assert "__EGL_VENDOR_LIBRARY_FILENAMES" not in e
    finally:
        del os.environ["NCZ_SCREENSAVER_SWITCHEROOCTL"]


# ---- planning: which GPU renders a hack --------------------------------------------------------
def classes_pegasus(env):
    layout(env, "nvidia+intel")
    disp, nv = env.list_gpus()
    seed_cache(
        env,
        {
            disp["id"]: {"class": "weak", "ms": 46.5},
            nv["id"]: {"class": "strong", "ms": 2.7, "copy_ms": 1.0},
        },
    )
    return disp, nv


def test_plan_light_hack_stays_on_igpu_heavy_goes_to_nvidia(env):
    disp, nv = classes_pegasus(env)
    light = env.plan_for("light_gles3", S)
    heavy = env.plan_for("heavy_gles3", S)
    medium = env.plan_for("medium_gles3", S)
    assert not light["offload"] and light["gpu"]["id"] == disp["id"]
    assert (
        heavy["offload"]
        and heavy["gpu"]["id"] == nv["id"]
        and heavy["class"] == "strong"
    )
    assert medium["offload"]


def test_plan_on_battery_keeps_everything_on_the_igpu(env):
    layout(env, "nvidia+intel", ac=False)
    disp, nv = env.list_gpus()
    seed_cache(env, {disp["id"]: {"class": "weak"}, nv["id"]: {"class": "strong"}})
    assert env.ac_online() is False
    assert env.offload_targets(S) == []
    assert not env.plan_for("heavy_gles3", S)["offload"]
    assert env.pool_class(S) == "weak"
    # an explicit choice of the discrete GPU is honoured on battery
    forced = {**S, "gpu-offload": nv["id"]}
    assert env.plan_for("light_gles3", forced)["offload"]


def test_plan_never_offloads_to_a_slower_gpu(env):
    layout(env, "amd-dgpu-display+intel-idle")
    amd, intel = env.list_gpus()
    seed_cache(env, {amd["id"]: {"class": "strong"}, intel["id"]: {"class": "weak"}})
    assert not env.plan_for("heavy_gles3", S)["offload"]
    forced = {**S, "gpu-offload": intel["id"]}
    p = env.plan_for("heavy_gles3", forced)
    assert p["offload"] and p["gpu"]["id"] == intel["id"]
    e = env.gpu_env(p["gpu"])
    assert e["DRI_PRIME"] == "pci-0000_00_02_0"


def test_gpu_offload_off_disables_targets(env):
    _disp, _nv = classes_pegasus(env)
    assert env.offload_targets({**S, "gpu-offload": "off"}) == []
    assert not env.plan_for("heavy_gles3", {**S, "gpu-offload": "off"})["offload"]


def test_prime_forces_offload_for_every_hack(env):
    classes_pegasus(env)
    assert env.plan_for("light_gles3", {**S, "gpu-offload": "prime"})["offload"]


def test_child_env_has_offload_variables_only_for_the_offloaded_hack(env):
    classes_pegasus(env)
    heavy = env.build_child_env(
        "heavy_gles3", S, base_env={"DRI_PRIME": "1", "HOME": str(env.tmp)}
    )
    light = env.build_child_env(
        "light_gles3",
        S,
        base_env={"__NV_PRIME_RENDER_OFFLOAD": "1", "HOME": str(env.tmp)},
    )
    assert heavy["__NV_PRIME_RENDER_OFFLOAD"] == "1" and "DRI_PRIME" not in heavy
    assert "__NV_PRIME_RENDER_OFFLOAD" not in light
    assert "__EGL_VENDOR_LIBRARY_FILENAMES" not in heavy


def test_weak_gpu_default_render_scale_and_high_override(env):
    _disp, _nv = classes_pegasus(env)
    off = {**S, "gpu-offload": "off"}
    shader = "hyprsaver_aurora_gles3"
    assert env.render_env(shader, off)["NCZ_RENDER_SCALE"] == "0.50"
    fixed = {**off, "render-scale-mode": "fixed", "render-scale": 1.0}
    assert "NCZ_RENDER_SCALE" not in env.render_env(shader, fixed)
    strong = {**S, "gpu-offload": "prime"}
    assert "NCZ_RENDER_SCALE" not in env.render_env(shader, strong)


def test_copy_cost_lowers_scale_on_the_offload_path(env):
    layout(env, "nvidia+intel")
    disp, nv = env.list_gpus()
    seed_cache(
        env,
        {disp["id"]: {"class": "weak"}, nv["id"]: {"class": "strong", "copy_ms": 12.0}},
    )
    got = env.render_env("hyprsaver_aurora_gles3", {**S, "gpu-offload": "prime"})
    assert got["NCZ_RENDER_SCALE"] == "0.75"


# ---- pools -----------------------------------------------------------------------------------
IDS = ["light_gles3", "medium_gles3", "heavy_gles3"]


def test_pool_weak_system_only_weak_ok(env):
    layout(env, "single-soc")
    seed_display(env, "weak")
    assert env.filter_pool(IDS, S) == ["light_gles3"]


def test_pool_mid_system_weak_and_mid(env):
    layout(env, "single-soc")
    seed_display(env, "mid")
    assert env.filter_pool(IDS, S) == ["light_gles3", "medium_gles3"]


def test_pool_strong_system_hides_nothing(env):
    layout(env, "mux-dgpu-only")
    seed_display(env, "strong")
    assert env.filter_pool(IDS, S) == IDS


def test_pool_weak_igpu_with_offload_target_gets_everything(env):
    classes_pegasus(env)
    assert env.filter_pool(IDS, S) == IDS


def test_pool_explicit_choices(env):
    layout(env, "single-soc")
    seed_display(env, "weak")
    assert env.filter_pool(IDS, {**S, "pool-gpu-class": "all"}) == IDS
    assert env.filter_pool(IDS, {**S, "pool-gpu-class": "mid"}) == [
        "light_gles3",
        "medium_gles3",
    ]
    assert env.filter_pool(IDS, {**S, "pool-gpu-class": "igpu-only"}) == ["light_gles3"]


def test_pool_excludes_broken_hacks_and_unknown_hacks_count_as_strong(env, monkeypatch):
    layout(env, "single-soc")
    seed_display(env, "weak")
    broken = env.tmp / "broken.tsv"
    broken.write_text("light_gles3\tbroken\treason\t2026-09-29\tx\n")
    monkeypatch.setenv("NCZ_SCREENSAVER_BROKEN", str(broken))
    got = env.filter_pool(["light_gles3", "medium_gles3", "brand_new_gles3"], S)
    assert "light_gles3" not in got  # broken hacks never enter a pool
    # with nothing left that fits, the pool falls back to what is not broken
    assert got == ["medium_gles3", "brand_new_gles3"]
    assert env.pool_class(S) == "weak"
    assert env.load_tiers().get("brand_new_gles3") is None  # unknown counts as strong


# ---- expectations and flags ----------------------------------------------------------------------
def test_flag_and_expectation_text(env):
    layout(env, "single-soc")
    rows = env.load_tier_rows()
    assert rows["heavy_gles3"]["min"] == "strong"
    assert (
        env.expectation("heavy_gles3", "weak", rows) == "about 12 fps on Intel UHD 630"
    )
    assert env.expectation("medium_gles3", "mid", rows) == "about 48 fps on Mali-G720"
    assert env.expectation("nope_gles3", "weak", rows) == ""


def test_legacy_tier_names_still_load(env):
    (env.tmp / "tiers.tsv").write_text("a_gles3\tigpu\nb_gles3\tdiscrete\n")
    assert env.load_tiers() == {"a_gles3": "weak", "b_gles3": "strong"}


def test_copy_ms_command_records_cost_and_survives_recalibration(env, monkeypatch):
    layout(env, "nvidia+intel")
    _disp, nv = env.list_gpus()
    args = type("A", (), {"force": False, "json": True, "copy_ms": f"{nv['id']}=9.5"})()
    assert env.cmd_calibrate(args) == 0
    assert env.read_class_cache()[nv["id"]]["copy_ms"] == 9.5
    monkeypatch.setenv("FAKE_MS", "2.7")
    entry = env.gpu_class(S, nv, force=True)
    assert entry["copy_ms"] == 9.5


def test_pool_and_plan_commands(env, monkeypatch, capsys):
    layout(env, "single-soc")
    seed_display(env, "weak")
    settings = dict(env.DEFAULTS)
    monkeypatch.setattr(env, "load_settings", lambda: (settings, True))
    monkeypatch.setattr(env, "installed_ids", lambda s: list(IDS))
    monkeypatch.setattr(env, "known_id", lambda h, c: True)
    monkeypatch.setattr(env, "load_catalog", list)
    assert env.cmd_pool(type("A", (), {"json": True})()) == 0
    out = json.loads(capsys.readouterr().out)
    assert out["class"] == "weak" and out["ids"] == ["light_gles3"]
    assert set(out["excluded"]) == {"medium_gles3", "heavy_gles3"}
    assert env.cmd_plan(type("A", (), {"hack": "heavy_gles3", "json": True})()) == 0
    plan = json.loads(capsys.readouterr().out)
    assert plan["offload"] is False
    assert plan["min_class"] == "strong"
    assert plan["class"] == "weak"


def test_child_env_exports_the_class_of_the_rendering_gpu(env):
    classes_pegasus(env)
    heavy = env.build_child_env("heavy_gles3", S, base_env={"HOME": str(env.tmp)})
    light = env.build_child_env("light_gles3", S, base_env={"HOME": str(env.tmp)})
    assert heavy["NCZ_GPU_CLASS"] == "strong"  # offloaded to the NVIDIA GPU
    assert light["NCZ_GPU_CLASS"] == "weak"  # stays on the Intel iGPU
    mine = env.build_child_env(
        "light_gles3", S, base_env={"HOME": str(env.tmp), "NCZ_GPU_CLASS": "mid"}
    )
    assert mine["NCZ_GPU_CLASS"] == "mid"  # an explicit value wins


def test_mid_class_uses_measured_render_hints(env, monkeypatch):
    layout(env, "single-soc")
    seed_display(env, "mid")
    hints = env.tmp / "hints.tsv"
    hints.write_text("hyprsaver_slow_gles3\t0.5\t60\tnote\n")
    monkeypatch.setenv("NCZ_SCREENSAVER_RENDER_HINTS", str(hints))
    got = env.render_env("hyprsaver_slow_gles3", S)
    assert got["NCZ_RENDER_SCALE"] == "0.50"
    assert "NCZ_RENDER_SCALE" not in env.render_env("hyprsaver_other_gles3", S)
    fixed = {**S, "render-scale-mode": "fixed", "render-scale": 1.0}
    assert "NCZ_RENDER_SCALE" not in env.render_env("hyprsaver_slow_gles3", fixed)


def test_on_ac_every_shader_hack_goes_to_the_discrete_gpu(env):
    _disp, nv = classes_pegasus(env)
    shader = env.plan_for("hyprsaver_light_gles3", S)
    assert shader["offload"] and shader["gpu"]["id"] == nv["id"]
    classic = env.plan_for("light_gles3", S)
    assert not classic["offload"]


def test_on_battery_no_hack_offloads_even_shader_hacks(env):
    layout(env, "nvidia+intel", ac=False)
    disp, nv = env.list_gpus()
    seed_cache(env, {disp["id"]: {"class": "weak"}, nv["id"]: {"class": "strong"}})
    for hid in ("hyprsaver_light_gles3", "heavy_gles3", "light_gles3"):
        assert not env.plan_for(hid, S)["offload"]
    child = env.build_child_env(
        "hyprsaver_light_gles3", S, base_env={"HOME": str(env.tmp)}
    )
    assert "__NV_PRIME_RENDER_OFFLOAD" not in child and "DRI_PRIME" not in child


def test_unmeasurable_hack_gets_a_floor_expectation(env):
    layout(env, "single-soc")
    (env.tmp / "tiers.tsv").write_text(
        "slow_gles3\tstrong\t-\t-\t-\t-\t-\t2026-09-29 test\n"
    )
    rows = env.load_tier_rows()
    assert env.expectation("slow_gles3", "weak", rows) == "under 6 fps on Intel UHD 630"


# ---- regressions from the code review ---------------------------------------------------------
def test_supervisor_plan_reaches_the_child_env_with_copy_cost(env):
    layout(env, "nvidia+intel")
    disp, nv = env.list_gpus()
    seed_cache(
        env,
        {disp["id"]: {"class": "weak"}, nv["id"]: {"class": "strong", "copy_ms": 12.0}},
    )
    plan = env.plan_for("hyprsaver_light_gles3", S)
    child = env.build_child_env(
        "hyprsaver_light_gles3", S, base_env={"HOME": str(env.tmp)}, offload=plan
    )
    assert child["__NV_PRIME_RENDER_OFFLOAD"] == "1"
    assert child["NCZ_RENDER_SCALE"] == "0.75"  # the copy cost lowers the scale
    assert child["NCZ_GPU_CLASS"] == "strong"


def test_chosen_gpu_id_is_not_overridden_by_a_better_gpu(env):
    layout(env, "amd-apu+amd-dgpu")
    add_gpu(env.sysfs, "card2", "0x10de", "0x1f08", "nvidia", "0000:08:00.0")
    env.list_gpus.cache_clear()
    gpus = env.list_gpus()
    amd_dgpu = next(g for g in gpus if g["id"] == "pci-0000_03_00_0")
    nvidia = next(g for g in gpus if g["vendor"] == "nvidia")
    seed_cache(
        env,
        {
            gpus[0]["id"]: {"class": "mid"},
            amd_dgpu["id"]: {"class": "strong"},
            nvidia["id"]: {"class": "strong"},
        },
    )
    chosen = {**S, "gpu-offload": amd_dgpu["id"]}
    child = env.build_child_env(
        "heavy_gles3", chosen, base_env={"HOME": str(env.tmp)}, offload=True
    )
    assert child["DRI_PRIME"] == "pci-0000_03_00_0"
    assert "__NV_PRIME_RENDER_OFFLOAD" not in child


def test_egl_vendor_pin_is_dropped_for_offloaded_hacks_only(env):
    classes_pegasus(env)
    base = {"HOME": str(env.tmp), "__EGL_VENDOR_LIBRARY_FILENAMES": "/x/50_mesa.json"}
    heavy = env.build_child_env("heavy_gles3", S, base_env=base)
    light = env.build_child_env("light_gles3", S, base_env=base)
    assert "__EGL_VENDOR_LIBRARY_FILENAMES" not in heavy
    assert (
        light["__EGL_VENDOR_LIBRARY_FILENAMES"] == "/x/50_mesa.json"
    )  # display GPU keeps its pin


def test_failed_benchmark_is_retried_later_not_cached_forever(env, monkeypatch):
    layout(env, "single-soc")
    calls = []
    real = env._calibrator_json

    def flaky(args, environ, timeout):
        calls.append(tuple(args))
        if not args:  # the benchmark itself fails
            return 2, None
        return real(args, environ, timeout)

    monkeypatch.setattr(env, "_calibrator_json", flaky)
    first = env.gpu_class(S, None)
    assert first["source"] == "table"
    n = len(calls)
    env.gpu_class(S, None)  # inside the retry window: no second benchmark attempt
    assert () not in calls[n:]
    cache = env.read_class_cache()
    key = next(iter(cache))
    cache[key]["retry_after"] = 0
    env.class_cache_path().write_text(json.dumps({"entries": cache}))
    monkeypatch.setattr(env, "_calibrator_json", real)
    assert env.gpu_class(S, None)["source"] == "calibration"


def test_render_only_gpu_takes_the_display_role_of_a_display_only_controller(env):
    # Pi-like: card0 is the display controller (no render node), card1 the render GPU
    add_gpu(env.sysfs, "card0", "0x0000", "0x0000", "vc4", "soc:gpu0", connected=True)
    add_gpu(env.sysfs, "card1", "0x0000", "0x0000", "v3d", "soc:gpu1")
    (env.sysfs / "class/drm/card1/device/drm/renderD128").mkdir(parents=True)
    add_power(env.sysfs, True)
    for fn in (env.list_gpus, env.switcheroo_envs, env.gpu_topology):
        fn.cache_clear()
    gpus = env.list_gpus()
    assert [g["driver"] for g in gpus] == ["v3d"]
    assert gpus[0]["display"] is True
    assert env.offload_targets(S) == []


def test_unknown_gpu_offload_value_behaves_like_auto_including_on_battery(env):
    layout(env, "nvidia+intel", ac=False)
    disp, nv = env.list_gpus()
    seed_cache(env, {disp["id"]: {"class": "weak"}, nv["id"]: {"class": "strong"}})
    for bogus in ("nvidia", "PCI-0000_01_00_0", ""):
        settings = {**S, "gpu-offload": bogus}
        assert env.offload_mode(settings) == "auto"
        assert env.offload_targets(settings) == []
        assert env.pool_class(settings) == "weak"


def test_calibration_happens_inside_the_supervisor_after_state_exists(env, monkeypatch):
    layout(env, "single-soc")
    order = []
    monkeypatch.setattr(
        env.Supervisor,
        "write_state",
        lambda self, rotates_at=None: order.append("state"),
    )
    monkeypatch.setattr(
        env.Supervisor, "calibrate", lambda self: order.append("calibrate")
    )
    monkeypatch.setattr(env.Supervisor, "pick", lambda self: None)
    args = type(
        "A",
        (),
        {"seconds": 0, "hack": None, "idle": False, "force": False, "foreground": True},
    )()
    sup = env.Supervisor(args, dict(env.DEFAULTS), [])
    monkeypatch.setattr(env.signal, "signal", lambda *a: None)
    sup.run()
    assert order[:2] == ["state", "calibrate"]
