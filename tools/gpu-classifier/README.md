# ncz-screensavers GPU classifier — `fix/gpu-tier-mali-g720-2026-09-30`

## The bug (m66, MS-R1 / Sky1 / Mali-G720-Immortalis, 2026-09-30)

Host `192.168.207.66` (cixmini / MS-R1) was reinstalled with
`ncz-os-arm64-20260929-4bd67b5f` (NCZ-OS 26.7 Maximilian, kernel 7.3.0-rc5-sky1).
Hardware confirmed working at the driver/userspace level:

```
$ lsmod | grep -i mali
mali_kbase           1269760  35

$ ls -la /dev/dri /dev/mali*
/dev/dri/card0  ... linlondp
/dev/dri/card1  ... linlondp
/dev/dri/card2  ... linlondp   (display=True)
/dev/dri/card3  ... linlondp
/dev/mali0      crw-rw-rw- 1 root 10,262

$ basename $(readlink /sys/class/misc/mali0/device/driver)
mali                                                  <-- platform driver name
# (NOT "mali_kbase" — that's the kernel-module name; the platform-driver
#  symlink resolves to "mali". Both names must mean "hardware GPU bound".)

$ vulkaninfo --summary | grep deviceName
  deviceName         = Mali-G720-Immortalis
  driverID           = DRIVER_ID_ARM_PROPRIETARY
  driverName         = Mali-G720-Immortalis

$ eglinfo | grep -E "renderer|version"
EGL vendor string: ARM
EGL version string: 1.5 Valhall-"r53p0-00eac0"
OpenGL ES profile renderer: Mali-G720-Immortalis
OpenGL ES profile version: OpenGL ES 3.2 v1.r53p0-00eac0.c707efa0cfa034b363bc93f9b6749cb5
```

But the screensaver launcher (`/usr/bin/ncz-screensaver 0.7.1`) classifies
this hardware as **weak** when the operator runs a fresh probe from an SSH
session. The Singularity launcher settings UI shows that verdict on a panel
labelled "GPU class". That is wrong: a Mali-G720-Immortalis with
`mali_kbase` loaded and Vulkan returning a real `Mali-G720-Immortalis`
device is the canonical mid-class (per `tiers.tsv` min-class column).

### Where the wrong verdict comes from (three bugs, all hit on .66)

The classifier path in `ncz-screensaver` runs the calibrator
(`/usr/libexec/ncz-screensavers/ncz-screensaver-calibrate`):

#### Bug A — the env-builder doesn't pin the CIX loader path

1. `gpu_class()` calls `_calibrator_json(["--identify"], env, 30)`. The
   `env` is `build_child_env("calibrate", ...)` — which copies `os.environ`
   and adds a few keys via `find_compositor_environ`, **but does not set
   `LD_LIBRARY_PATH`** to the CIX proprietary path
   (`/opt/cixgpu-pro/lib/aarch64-linux-gnu`). On the live .66 host there is
   no compositor running in the SSH session (user `mini` is on `tty1`, no
   `WAYLAND_DISPLAY`, no `labwc`/`sway` in the process table), so
   `find_compositor_environ` returns `{}`.
2. The calibrator subprocess inherits the parent shell's `LD_LIBRARY_PATH`
   (typically empty). `libEGL.so.1` resolves to **system Mesa 26**
   (`/usr/lib/aarch64-linux-gnu/libEGL.so.1`), whose only backing driver on
   this image is `llvmpipe`. The `MESA_LOADER_DRIVER_OVERRIDE=zink` baked
   into the binary fails with `MESA: error: ZINK: failed to choose pdev` and
   `libEGL warning: DRI2: failed to create screen`.
3. The calibrator exits with code 2 on .66 today (`calibrate: no GLES3 config`).
   (The class-3 refusal branch is documented but the live reproducer hits
   class 2 — see Bug B for why this still gives "weak".)

#### Bug B — the topology classifier can't see the misc/mali0 entry

The launcher's `list_gpus()` enumerates `/sys/class/drm/card*` only. On
Sky1 the 4 `/dev/dri/card*` devices are bound to `linlondp` (the display
controller), NOT the 3D GPU. The actual Mali-G720 is at
`/sys/class/misc/mali0/device/driver -> mali`. The launcher misses it.

When the calibrator fails and `gpu_class()` falls through to
`fallback_entry("", gpu)`:

```python
def class_from_topology(gpu=None):
    if gpu is None: return "weak"
    if gpu["discrete"]: return "strong"
    if gpu["driver"] in ("i915", "xe", "v3d", "vc4", "lima", "etnaviv"):
        return "weak"
    if gpu["driver"] in ("mali_kbase", "panthor", "panfrost", "msm", "amdgpu"):
        return "mid"
    return "weak"        # <-- linlondp hits this; returns "weak"
```

`display_gpu.driver == "linlondp"` is NOT in any list, so it returns
**weak** — even though `/dev/mali0` is bound to `mali` and `mali_kbase`
is loaded. The launcher's own cache
(`/home/mini/.cache/ncz-screensavers/gpu-class.json`) ALREADY records the
misc entry (`soc-CIXH5000_00`, driver=mali, class=mid) — proving the
launcher has seen the data but doesn't consult it at classification time.

#### Bug C — the code-3 refusal is unconditional

Even the documented `if code == 3:` path returns weak without consulting
the bound driver:

```python
code, ident = _calibrator_json(["--identify"], env, 30)
if code == 3:
    return {"class": "weak", "source": "refused", "software": True}
```

This is correct on a real software-only box (server, headless VM, CI
container). It is WRONG on a host where the real GPU is bound (`.66`).

### Why all three matter

| Caller | Bug A (env) | Bug B (topology) | Bug C (refusal) |
|---|---|---|---|
| SSH session, fresh probe | hit (calibrator exit 2) | hit (linlondp) | bypassed |
| SSH session, code-3 path | hit (calibrator exit 3) | hit (linlondp) | hit |
| Compositor alive | not hit | hit (linlondp) | bypassed |

All three need to be addressed to fix the operator-visible "weak" verdict.

### Verification on the live host

```
$ /usr/libexec/ncz-screensavers/ncz-screensaver-calibrate --identify
MESA: error: ZINK: failed to choose pdev
libEGL warning: egl: failed to create dri2 screen
libEGL warning: DRI2: failed to create screen
calibrate: no GLES3 config
$ echo $?
2

$ # With the right loader path (what the launcher SHOULD inject on Sky1):
$ LD_LIBRARY_PATH=/opt/cixgpu-pro/lib/aarch64-linux-gnu \
  __EGL_VENDOR_LIBRARY_FILENAMES=/opt/cixgpu-compat/share/glvnd/egl_vendor.d/40_cix.json \
  NCZ_GPU_BACKEND=mali \
  /usr/libexec/ncz-screensavers/ncz-screensaver-calibrate --identify
{"renderer":"Mali-G720-Immortalis","version":"OpenGL ES 3.2 v1.r53p0-00eac0.c707efa0cfa034b363bc93f9b6749cb5","platform":"default","identify":true}
# rc=0

$ # Full benchmark with the right loader path:
$ LD_LIBRARY_PATH=/opt/cixgpu-pro/lib/aarch64-linux-gnu \
  __EGL_VENDOR_LIBRARY_FILENAMES=/opt/cixgpu-compat/share/glvnd/egl_vendor.d/40_cix.json \
  NCZ_GPU_BACKEND=mali \
  __EGL_PLATFORM=surfaceless \
  /usr/libexec/ncz-screensavers/ncz-screensaver-calibrate
{"renderer":"Mali-G720-Immortalis","version":"OpenGL ES 3.2 v1.r53p0-00eac0.c707efa0cfa034b363bc93f9b6749cb5","platform":"default","timer":"wall","frames":31,"ms":17.812}
# 17.812ms -> class_from_ms returns "mid" (8 <= 17.812 < 20)
```

The renderer string and benchmark timing are real numbers from this host
on 2026-09-30 05:25 UTC. They are embedded as constants in
`tools/gpu-classifier/tests/test_classifier.py` so the regression is
locked without needing a live box to re-verify.

## The fix

Three-part, minimal, self-contained. All three pieces live in
`tools/gpu-classifier/` in this branch and are designed to be spliced into
the upstream Python launcher (`/usr/bin/ncz-screensaver`) via the unified
diff in `patches/launcher-gpu-class.patch`.

1. **Loader-path env builder (`calibrator_env.py`).** Fixes Bug A. A helper
   the launcher calls when constructing the calibrator's environment. On a
   Sky1-arm64 platform (detected via `platform_id()` returning
   `"sky1-arm64"`) it sets `LD_LIBRARY_PATH` and the CIX vendor pin so the
   subprocess picks up
   `libEGL.so.1 -> libEGL_cix.so.1 -> libmali.so`, the same way
   `/usr/local/bin/ncz-gpu-env` does for the desktop session. On other
   platforms it is a no-op.

2. **Refusal-aware fallback + misc/mali0 discovery
   (`gpu_classifier.py`).** Fixes Bugs B and C. The module exposes:

    * `discover_gpus()` — enumerates BOTH `/sys/class/drm/card*` AND
      `/sys/class/misc/mali*`, tagging each entry with `sysfs_kind` so
      downstream code can tell which bus it came from.
    * `driver_is_hardware_gpu(driver, sysfs_kind)` — the predicate that
      answers "is this a real 3D GPU bound to this device?", with separate
      lists for DRM cards and misc devices.
    * `driver_is_display_controller(driver)` — the complement, "is this a
      display-only KMS / scanout controller?" — covers linlondp, komeda,
      vkms, etc.
    * `class_from_topology(topo_or_dict)` — verbatim from the launcher,
      extended to use the new driver lists; returns `mid` for `mali`,
      `mali_kbase`, `panthor`, `panfrost`, `msm`; `weak` for iGPU
      Intel/VC4/V3D/etc.; `strong` for discrete.
    * `classify_from_calibrator_result(code, gpu, ..., all_gpus=...)` —
      the refusal-aware classifier. Branches:
      - code 0 + positive ms → calibration path (unchanged).
      - code in (1, 2, 4) → retry-later path, consults the misc/mali0
        entry when available.
      - code 3 + hardware bound → mid/strong via topology + renderer-table
        (the fix).
      - code 3 + no hardware bound → weak (preserved contract).

3. **Splice patch (`patches/launcher-gpu-class.patch`).** A unified diff
   against `/usr/bin/ncz-screensaver 0.7.1` that splices in:
    - the import block (try/except for `classify_from_calibrator_result`,
      `augment_calibrator_env`, `discover_gpus`),
    - the new `_all_known_gpus()` helper that merges list_gpus() +
      discover_gpus(),
    - the env augmentation call before the calibrator subprocess,
    - the refusal-aware return on code 3,
    - the refusal-aware return on the no-ident (code 2) path,
    - the refusal-aware fallback in `fallback_entry()` (the read-only
      path used by `ncz-screensaver doctor` / `ncz-screensaver gpus` when
      the cache is empty).

The patch was generated against the actual upstream file copied from
`/usr/bin/ncz-screensaver` on .66 (md5 `1bd3ee4b00e7dcd0acb83a1b17274135`)
and was round-tripped: dry-run applies, real apply produces a
parser-clean Python file, and the resulting patched launcher
(committed at `tests/fixtures/patched_launcher.py`) is what the
integration tests exercise end-to-end.

### Why this is the minimum

We could rewrite `ncz-screensavers` in Rust, but that would be a much
larger change with more risk of regressing the 84 catalog rows and the
dozens of existing screensavers. The bug is in **one** decision (the
refusal verdict) and **one** data source (the misc/mali0 entry). The fix
touches only those two sites plus the env builder.

We could add a `--probe-only` mode to the C calibrator, but the existing
`--identify` already returns the same data (renderer + version + platform)
without binding a benchmark surface. Adding a second flag would mean
touching the C source, which lives in the upstream `src/` tree and isn't
in this branch — that would have made the fix bigger than necessary and
increased the chance of drift between the github mirror and the gitlab
build repo.

## Files in this branch

* `tools/gpu-classifier/gpu_classifier.py` — the standalone classifier
  module (no GUI, no `ncz-screensaver` import, stdlib only).
* `tools/gpu-classifier/calibrator_env.py` — the Sky1 loader-path
  helper.
* `tools/gpu-classifier/ncz_screensaver_patch.py` — a Python-readable
  splice spec for the upstream launcher.
* `tools/gpu-classifier/patches/launcher-gpu-class.patch` — the
  unified-diff patch (`git am`-able on the gitlab mirror).
* `tools/gpu-classifier/verify_live_v66.py` — operator-facing script
  that prints the fix's verdict against the live .66 hardware state.
* `tools/gpu-classifier/tests/test_classifier.py` — 77 unit tests using
  the captured .66 strings (renderer="Mali-G720-Immortalis", driver="mali",
  driver="linlondp", the four DRM cards, the misc/mali0 entry).
* `tools/gpu-classifier/tests/test_calibrator_env.py` — 23 unit tests
  for the Sky1-only loader-path helper.
* `tools/gpu-classifier/tests/test_launcher_integration.py` — 7
  integration tests that load the patched launcher (committed as a
  fixture) and exercise `gpu_class()` end-to-end with mocked calibrator
  subprocess results.
* `tools/gpu-classifier/tests/build_patched_launcher_fixture.sh` — a
  helper that rebuilds the patched-launcher fixture from the live
  upstream source via ssh.
* `tools/gpu-classifier/tests/fixtures/patched_launcher.py` — the
  committed fixture (the patched upstream launcher).
* `tools/gpu-classifier/tests/fixtures/v66-live/STRINGS.txt` — the
  literal output of `vulkaninfo`, `eglinfo`, `lsmod`, `/sys/class/...`,
  and the calibrator reproducer, captured 2026-09-30 05:25 UTC from
  `.66`.
* `tools/gpu-classifier/EVIDENCE.md` — the literal command transcripts
  this README summarises, including timestamps and SHA1s.

## Running the tests locally

```
cd tools/gpu-classifier
python3 -m unittest discover -v tests
```

107 tests, all stdlib. Two are skipped on hosts that lack /sys (CI
containers, etc.). The integration tests load the patched-launcher
fixture and exercise the gpu_class() refusal / no-ident / happy-path
branches against the live .66 GPU state, mocked.

## Re-verifying on the live host

```
ssh mini@192.168.207.66
cd /tmp
python3 /tmp/verify_live_v66.py
```

Output includes a verdict for every classification branch (calibration
success, refusal with hardware, refusal without, software-only box,
discover_gpus live enumeration, Sky1 loader-path env builder).

The orchestrator brief says "Do not reboot, reinstall or change system
config on .66; no package installs there." — so we don't drop the
patched launcher in place. The classifier's correctness is locked in by
the unit tests + the live verifier, both of which run against the same
captured numbers the live host produced.

## Known limitations

* The new `classify_from_calibrator_result()` requires the launcher to
  pass `all_gpus=` (the union of DRM + misc) for the fix to fire. If the
  launcher only passes `gpu=` (the display_gpu dict), the misc/mali0
  entry is invisible and the answer may still be `weak` on Sky1. The
  patched launcher's `_all_known_gpus()` helper does this merge; the
  patch in `patches/launcher-gpu-class.patch` adds the helper and uses
  it at every gpu_class() call site.
* The `discover_gpus()` function enumerates `/sys/class/misc/mali*` only
  — Sky1 / CIX is the only platform in the NCZ-OS image where the GPU is
  at misc rather than under DRM. On other platforms (Pi, Rockchip,
  Allwinner, etc.) the misc path is empty and discover_gpus() returns
  the same list list_gpus() does.
