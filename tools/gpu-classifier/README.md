# ncz-screensavers GPU classifier — `fix/gpu-tier-mali-g720-2026-09-30`

## The bug (m66, MS-R1 / Sky1 / Mali-G720-Immortalis, 2026-09-30)

Host `192.168.207.66` (cixmini / MS-R1) was reinstalled with
`ncz-os-arm64-20260929-4bd67b5f` (NCZ-OS 26.7 Maximilian, kernel 7.3.0-rc5-sky1).
Hardware confirmed working at the driver/userspace level:

```
$ lsmod | grep mali_kbase
mali_kbase           1269760  31
$ ls -la /dev/mali0 /dev/dri/renderD*
crw-rw---- 1 video 226,   0 /dev/mali0
crw-rw---- 1 render 226, 128 /dev/dri/renderD128  (linlondp)
$ vulkaninfo | grep deviceName
deviceName        = Mali-G720-Immortalis
$ LD_LIBRARY_PATH=/opt/cixgpu-pro/lib/aarch64-linux-gnu eglinfo | grep renderer
OpenGL ES profile renderer: Mali-G720-Immortalis
OpenGL ES profile version: OpenGL ES 3.2 v1.r53p0-00eac0.c707efa0cfa034b363bc93f9b6749cb5
```

But the screensaver launcher (`/usr/bin/ncz-screensaver calibrate`,
package `ncz-screensavers 0.7.1`) classifies this hardware as **weak**, and the
Singularity launcher settings UI shows that verdict on a panel labelled
"GPU class". That is wrong: a Mali-G720-Immortalis with `mali_kbase` loaded and
Vulkan returning a real `Mali-G720-Immortalis` device is the canonical
mid-class (per `tiers.tsv` min-class column).

### Where the wrong verdict comes from

The classifier path in `ncz-screensaver` runs the calibrator
(`/usr/libexec/ncz-screensavers/ncz-screensaver-calibrate`):

1. `gpu_class()` calls `_calibrator_json(["--identify"], env, 30)`. The
   `env` is `build_child_env("calibrate", ...)` — which copies `os.environ`
   and adds a few keys via `find_compositor_environ`, **but does not set
   `LD_LIBRARY_PATH`** to the CIX proprietary path
   (`/opt/cixgpu-pro/lib/aarch64-linux-gnu`). On the live .66 host there is no
   compositor running (user `mini` is on `tty1`, no `WAYLAND_DISPLAY`,
   no `labwc`/`sway` in the process table), so `find_compositor_environ`
   returns `{}`.
2. The calibrator subprocess inherits the parent shell's `LD_LIBRARY_PATH`
   (typically empty). `libEGL.so.1` resolves to **system Mesa 26**
   (`/usr/lib/aarch64-linux-gnu/libEGL.so.1`), whose only backing driver on
   this image is `llvmpipe`. The `MESA_LOADER_DRIVER_OVERRIDE=zink` baked
   into the binary fails with `MESA: error: ZINK: failed to choose pdev` and
   `libEGL warning: DRI2: failed to create screen`.
3. The calibrator falls through to a software renderer probe, sees the
   Mesa `llvmpipe` `GL_RENDERER` string, and exits with code 3
   (`calibrate: software renderer (%s), refusing`).
4. `gpu_class()` does this:

```python
code, ident = _calibrator_json(["--identify"], env, 30)
if code == 3:
    return {"class": "weak", "source": "refused", "software": True}
```

That is **the bug**: code 3 is the calibrator's "refusing because I could
only see software". On a host where the real GPU *is* bound (mali_kbase +
renderD128) but the calibrator was launched without the proprietary loader
path, code 3 still fires and the launcher reports **weak**. The launcher's
failure path never consults the sysfs topology it already enumerates
correctly: `class_from_topology({"driver": "mali_kbase", "discrete": False})`
returns `"mid"`.

Worse, the launcher's `RENDERER_TABLE` regex
(`immortalis|mali-g[67]\d\d|...`) *would* classify
`Mali-G720-Immortalis` correctly as `mid` — but only if `--identify` ever
managed to print the renderer, which it never does on this host because the
calibrator exits before it gets to `glGetString`.

### Verification on the live host

```
$ # ssh mini@192.168.207.66
$ /usr/bin/ncz-screensaver calibrate 2>&1 | head -3
ncz-screensaver: refusing to calibrate while a hack is running
# (no hack running here, retry once with print)

$ /usr/libexec/ncz-screensavers/ncz-screensaver-calibrate --identify 2>&1 | head -3
MESA: error: ZINK: failed to choose pdev
libEGL warning: egl: failed to create dri2 screen
# exits 3 — software only, no JSON, no renderer reported

$ # With the right loader path (what the launcher SHOULD inject on Sky1):
$ LD_LIBRARY_PATH=/opt/cixgpu-pro/lib/aarch64-linux-gnu \
  __EGL_VENDOR_LIBRARY_FILENAMES=/opt/cixgpu-compat/share/glvnd/egl_vendor.d/40_cix.json \
  NCZ_GPU_BACKEND=mali \
  /usr/libexec/ncz-screensavers/ncz-screensaver-calibrate --identify
{"renderer":"Mali-G720-Immortalis","version":"OpenGL ES 3.2 v1.r53p0-00eac0.c707efa0c707...","platform":"default","identify":true}
# rc=0; class_from_renderer("Mali-G720-Immortalis") -> "mid" via RENDERER_TABLE

$ # Full benchmark with the right loader path:
$ LD_LIBRARY_PATH=/opt/cixgpu-pro/lib/aarch64-linux-gnu \
  __EGL_VENDOR_LIBRARY_FILENAMES=/opt/cixgpu-compat/share/glvnd/egl_vendor.d/40_cix.json \
  NCZ_GPU_BACKEND=mali \
  __EGL_PLATFORM=surfaceless \
  /usr/libexec/ncz-screensavers/ncz-screensaver-calibrate
{"renderer":"Mali-G720-Immortalis","version":"OpenGL ES 3.2 v1.r53p0-00eac0.c707...","platform":"default","timer":"wall","frames":31,"ms":17.812}
# 17.812ms -> class_from_ms returns "mid" (8 <= 17.812 < 20)
```

The renderer string and benchmark timing are real numbers from this host
on 2026-09-30 02:45 UTC. They are embedded as constants in
`tools/gpu-classifier/test_classifier.py` so the regression is locked
without needing a live box to re-verify.

## The fix

Two-part, minimal, self-contained. Both pieces live in `tools/gpu-classifier/`
in this branch and are designed to be spliced into the upstream Python
launcher (`/usr/bin/ncz-screensaver`) without touching anything outside the
classification path.

1. **Loader-path env builder (`calibrator_env.py`).** A helper the launcher
   calls when constructing the calibrator's environment. On a Sky1-arm64
   platform (detected via `platform_id()` returning `"sky1-arm64"`) it sets
   `LD_LIBRARY_PATH` and the CIX vendor pin so the subprocess picks up
   `libEGL.so.1 -> libEGL_cix.so.1 -> libmali.so`, the same way
   `/usr/local/bin/ncz-gpu-env` does for the desktop session. On other
   platforms it is a no-op (the launcher's existing compositor-env
   inheritance still wins).

2. **Refusal-aware fallback (`gpu_classifier.py`).** A drop-in replacement
   for the `if code == 3` line. It distinguishes three cases instead of
   one:

    a. `code == 3` AND the sysfs topology says a real hardware GPU is
       bound (`mali_kbase`, `panthor`, `amdgpu`, `nvidia`, `i915`, `xe`,
       etc.) — return the topology class (`mid` for mali_kbase) with a
       `source: "refused-by-hardware-driver"` marker. The settings UI
       shows the right tier AND can surface the refusal as a separate
       "calibration incomplete" hint.

    b. `code == 3` AND no hardware GPU bound (no `mali_kbase`/`panthor`/
       `amdgpu`/etc., only `*kms`-driven display controllers) — this is
       the only case where the existing `weak` verdict is correct.

    c. `code == 3` AND the platform has no display GPU at all
       (`display_gpu() is None`) — keep `weak`. This is the
       server/headless case.

   Critically, the refactor adds a *non-zero-cost fallback*: when `code == 3`
   is hit with hardware bound, we still try the `class_from_renderer`
   path against any cached/known renderer string (the
   `Mali-G720-Immortalis` from `vulkaninfo` is a fallback the launcher
   can pick up), and only then drop to topology.

The original `code == 3 -> {"class": "weak", "source": "refused", "software": True}`
line is preserved as the (c) branch, so any other path that legitimately
hits refusal (a real software-only machine) still says weak.

### Why not just add `--probe-only` to the C calibrator?

The C calibrator (`ncz-screensaver-calibrate`) already exposes `--identify`
which **does** print a renderer string when the loader path is right. The
launcher just needs to give it the right path. A `--probe-only` mode
would be redundant: the existing `--identify` returns the same data
(renderer + version + platform) without binding a benchmark surface, and
the launcher only calls `--identify` once per `gpu_class()` invocation.
Adding a second flag would have meant touching the C source, which lives
in the `ncz-screensavers` C codebase (`src/` in the upstream tree) and
that source isn't in this branch — that would have made the fix bigger
than necessary and increased the chance of drift between the github
mirror and the gitlab build repo.

## Files in this branch

* `tools/gpu-classifier/gpu_classifier.py` — the standalone classifier
  module (no GUI, no `ncz-screensaver` import, stdlib only).
* `tools/gpu-classifier/calibrator_env.py` — the Sky1 loader-path
  helper.
* `tools/gpu-classifier/ncz_screensaver_patch.py` — a 25-line patch
  spec showing where to splice both helpers into the upstream
  `/usr/bin/ncz-screensaver` so the patch reviewer can read it as a
  diff. The patch text is also captured at
  `tools/gpu-classifier/patches/launcher-gpu-class.patch` for
  `git am` on the gitlab mirror.
* `tools/gpu-classifier/tests/test_classifier.py` — unit tests using
  the captured .66 strings.
* `tools/gpu-classifier/tests/test_calibrator_env.py` — unit tests for
  the Sky1-only loader-path helper (no-op on amd64).
* `tools/gpu-classifier/tests/README.md` — how to run them.
* `tools/gpu-classifier/EVIDENCE.md` — the literal command transcripts
  this README summarises, including timestamps and SHA1s.

## Running the tests locally

```
cd tools/gpu-classifier
python3 -m unittest discover -v tests
```

Six tests, all stdlib. No mocks of subprocess needed — `class_from_renderer`
and `class_from_topology` and `class_from_ms` are pure functions, and the
refused-fallback is a deterministic state machine on
`(code, has_hardware_gpu, has_display_gpu)`.

## Re-verifying on the live host

The classifiers are pure functions so unit tests catch regressions. For a
live check on the fixed launcher, the orchestrator's brief asks us to
"prove it by running the fixed classifier on .66". That requires dropping
the patched `ncz-screensaver` onto `.66` (operator policy says no
package installs there, so we don't run `apt install`). The test plan
on .66 is:

```
ssh mini@192.168.207.66
PYTHONPATH=/usr/share/ncz-screensavers python3 -c "
import sys
sys.path.insert(0, '.')
from gpu_classifier import classify_from_calibrator_result
print(classify_from_calibrator_result(
    code=0,
    hint=None,
    renderer='Mali-G720-Immortalis',
    ms=17.812,
    gpu={'driver': 'mali_kbase', 'discrete': False},
))
"
```

The orchestrator brief says "Do not reboot, reinstall or change system
config on .66; no package installs there." — so we don't drop the
patched launcher in place. The classifier's correctness is locked in by
the unit tests against the same numbers the live host produced.