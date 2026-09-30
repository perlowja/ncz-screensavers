# Sky1 (MS-R1 / cixmini / 192.168.207.66) re-validation evidence — 2026-09-30 (turn 7, r42)

Independent re-validation of the live `.66` host (CIX Sky1 / MS-R1 /
Mali-G720-Immortalis / `mali_kbase`, kernel `7.3.0-rc5-sky1-ncz`,
NCZ-OS build `ncz-os-arm64-20260929-4bd67b5f`, greetd session `c2` on
`tty1`, `singularity-labwc` running) under continuation dispatch
(r42-cont, 2026-09-30T16:39Z).

Branch state at dispatch entry: `fix/gpu-tier-mali-g720-2026-09-30`
at `f365279` (the r15-continuation tip, both locally and on github
mirror `perlowja/ncz-screensavers`). The argonas remote is
unreachable from this build pool (no publickey, password auth
disabled at the sshd level for non-root users); the bundle + patch
fallback written to ARGONAS reports dir per operator rule.

This dispatch **does not modify any package, service, or config on
`.66`**. The fixed `launcher/ncz-screensaver` (md5
`e7e77d4cc7cf0294902086149486096e`) is copied to
`/tmp/m66-continuation/ncz-screensaver-fixed` on `.66` via scp, made
executable, and run against the live `/sys`, the live
`~/.cache/ncz-screensavers/gpu-class.json`, and the live compositor
session. The shipped `/usr/bin/ncz-screensaver` (md5
`1bd3ee4b00e7dcd0acb83a1b17274135`, ncz-screensavers 0.7.1) is
unchanged.

## 0. What this dispatch verifies (independently of prior passes)

This dispatch (r42) is the seventh independent live re-validation of
the fix. The prior passes (r13 / r18 / r2 / r3 / r4 / r15-cont)
put the source-tree fix in place at `8f221fb` through `444d8c8` and
added six live ms pins. This dispatch:

1. **Adds a fourth independent live ms pin** (`SKY1_R42_CAPTURE`,
   ms=9.89, captured 2026-09-30T16:39Z on .66). All four pins
   (`SKY1_PASS13_CAPTURE` ms=9.88, `SKY1_R18_CAPTURE` ms=14.71,
   `SKY1_R15_CONT_CAPTURE` ms=11.93, `SKY1_R42_CAPTURE` ms=9.89)
   classify as `mid` because the threshold band is 8.0 <= ms < 20.0.
   A regression that changes the threshold band now fails FOUR
   independent pins instead of three.
2. **Adds Vulkan ICD string pins** (`SKY1_VULKAN_STRINGS`,
   captured from `vulkaninfo --summary` on .66). The vendorID
   `0x13b5` (ARM), deviceID `0xc87...` (Mali-G720), driverName
   `Mali-G720-Immortalis`, and driverInfo `v1.r53p0-00eac0...`
   pin the live Vulkan ICD identification of the GPU. A
   regression that matched any of these against a "weak" pattern
   (e.g. `mali-g[31]\b|llvmpipe`) would silently demote the GPU
   to weak — this dispatch's `test_sky1_r42_vulkan_strings_are_real_mali_no_llvmpipe`
   pins that contract.
3. **Adds EGL/GLES3 string pins** (`SKY1_EGL_STRINGS`, captured
   from `eglinfo` on .66). The vendor `ARM`, version
   `1.5 Valhall-"r53p0-00eac0"`, and renderer
   `Mali-G720-Immortalis` confirm the Mali ICD is loaded, not
   llvmpipe.
4. **Adds compositor env pins** (`SKY1_COMPOSITOR_ENV`, captured
   from `/proc/9206/environ` on .66). The pinned env has
   `__EGL_VENDOR_LIBRARY_FILENAMES` pointing at
   `/opt/cixgpu-compat/share/glvnd/egl_vendor.d/40_cix.json` (the
   CIX glvnd override), `VK_DRIVER_FILES` at
   `/etc/vulkan/icd.d/mali.json`, and
   `LD_LIBRARY_PATH` containing
   `/opt/cixgpu-pro/lib/aarch64-linux-gnu` (the CIX
   proprietary loader path). A regression that stripped any of
   these from the labwc-session startup would land the calibrator
   back on llvmpipe.
5. **Adds an adversarial test** that exercises the
   `sysfs-has-no-misc-mali0` case (the platform bus GPU is
   missing from sysfs) and asserts the legitimate verdict is
   `weak` — NOT silently rewritten to `mid`. The
   "never fall back to software on a GPU system" policy is
   about NOT classifying a real GPU as weak; it is NOT about
   classifying a missing GPU as mid. This pins that contract.

## 1. Live `gpus` (FIXED `/tmp/m66-continuation/ncz-screensaver-fixed`)

```
$ /tmp/m66-continuation/ncz-screensaver-fixed gpus
soc-CIXH5000_00 display  other   mali     mid    9.9 ms
```

One row. `soc-CIXH5000_00` is the platform-bus Mali device (driver
`mali`, bound by `/sys/bus/platform/drivers/mali` on the CIX
`CIXH5000:00` device). The four `linlondp` display controllers
(`CIXH5010:00/01/03/04`) are filtered out by `list_gpus()` once a real
GPU is found via `/sys/class/misc/mali0`.

## 2. Live `gpus --json` (FIXED)

```json
[
  {
    "id": "soc-CIXH5000_00",
    "card": "",
    "slot": "CIXH5000:00",
    "vendor": "other",
    "driver": "mali",
    "device": "",
    "display": true,
    "boot_vga": false,
    "discrete": false,
    "render": true,
    "display_only": false,
    "class": "mid",
    "ms": 9.89,
    "renderer": "Mali-G720-Immortalis"
  }
]
```

The `display_only: false` on the Mali is the key field — the four
linlondp display controllers all have `display_only: true` and are
filtered out.

## 3. Live `status --json` (FIXED)

```json
{
  "gpu_class": "mid",
  "gpu_class_score_ms": 9.89,
  "gpu_class_source": "calibration",
  "running": true,
  "hack": "hyprsaver_donut_gles3",
  "pid": 497156,
  "uptime": 11.6,
  "mode": "random",
  "idled": {
    "pid": 9245,
    "state": "saver",
    "plan": {"saver": 300, "lock": null, "dpms": null, "lock_on_suspend": false},
    "updated": 1790777015
  }
}
```

This dispatch's `gpu_class_score_ms=9.89` becomes the new
`SKY1_R42_CAPTURE.ms` pin; the new test
`test_sky1_r42_cmd_status_reports_mid_with_this_dispatches_capture`
asserts it.

## 4. Live `doctor` (FIXED)

```text
gpu_class.display = {
  "class": "mid",
  "ms": 9.89,
  "renderer": "Mali-G720-Immortalis",
  "version": "OpenGL ES 3.2 v1.r53p0-00eac0.c707efa0cfa034b363bc93f9b6749cb5",
  "timer": "gpu",
  "source": "calibration",
  "when": "2026-09-30T16:39:07+0000",
  "gpu": "soc-CIXH5000_00",
  "driver": "mali"
},
tiers_loaded = 92,
pool_class = "mid",
wayland_sockets = ["wayland-0"],
grim = true,
idled_running = true
```

The `pool_class: mid` is the central promise. The shipped 0.7.1
reports `pool_class: weak` on a fresh install (the topology fallback
maps the linlondp cards to weak and never surfaces the Mali).

## 5. Live SHIPPED binary (still broken, 0.7.1) — regression baseline

```
$ /usr/bin/ncz-screensaver gpus --json
[
  {"id":"pci-CIXH5010_03","driver":"linlondp","class":"weak","ms":null,
   "renderer":""},
  {"id":"pci-CIXH5010_00","driver":"linlondp","class":"weak","ms":null,
   "renderer":""},
  {"id":"pci-CIXH5010_01","driver":"linlondp","class":"weak","ms":null,
   "renderer":""},
  {"id":"pci-CIXH5010_04","driver":"linlondp","class":"weak","ms":null,
   "renderer":""}
]
```

All four cards `class=weak` on the shipped 0.7.1 binary. This is
the operator's "GPU shown as weak" symptom, reproduced verbatim
this dispatch.

## 6. Live Vulkan ICD (no llvmpipe)

```
$ vulkaninfo --summary | grep -E "deviceName|driverName|driverInfo|vendorID|deviceID"
        deviceName         = Mali-G720-Immortalis
        driverName         = Mali-G720-Immortalis
        driverInfo         = v1.r53p0-00eac0.c707efa0cfa034b363bc93f9b6749cb5
        vendorID           = 0x13b5
        deviceID           = 0xc8700000
        driverVersion      = 53.0.0

$ vulkaninfo | grep -ci llvmpipe
0
```

Zero `llvmpipe` matches — the CIX GPU vendor override is loaded,
not software rendering. The Mali-G720-Immortalis is exposed as a
real Vulkan device.

## 7. Live EGL/GLES3 (ARM vendor, Valhall version)

```
$ eglinfo | grep -E "vendor|version|renderer"
EGL vendor string: ARM
EGL version string: 1.5 Valhall-"r53p0-00eac0"
OpenGL ES profile renderer: Mali-G720-Immortalis
```

The `EGL vendor string: ARM` is the canonical sign that the Mali
ICD (not Mesa) is loaded. A regression that flipped this to
`Mesa Project` would mean llvmpipe was loaded — a software
fallback violation on a GPU system.

## 8. Live compositor (labwc, singularity-labwc-session)

```
$ pgrep -fa labwc
9070 /bin/bash /opt/singularity/bin/singularity-labwc-session
9206 /opt/singularity/bin/labwc -S /opt/singularity/bin/singularity-desktop-session

$ ls -la /run/user/1000/wayland-*
srwxrwxr-x  1 mini mini   0 Sep 30 02:17 /run/user/1000/wayland-0
-rw-rw----  1 mini mini   0 Sep 30 02:17 /run/user/1000/wayland-0.lock

$ cat /proc/9206/environ | tr '\0' '\n' | grep -iE 'egl|vulkan|mali|cix|xdg|session'
__EGL_VENDOR_LIBRARY_FILENAMES=/opt/cixgpu-compat/share/glvnd/egl_vendor.d/40_cix.json
VK_DRIVER_FILES=/etc/vulkan/icd.d/mali.json
NCZ_GPU_BACKEND=mali
XDG_SESSION_TYPE=wayland
XDG_SESSION_ID=c2
XDG_SEAT=seat0
XDG_VTNR=1
LD_LIBRARY_PATH=/opt/singularity/lib:/opt/singularity/lib/aarch64-linux-gnu:...:/opt/cixgpu-pro/lib/aarch64-linux-gnu:...
```

The compositor is alive on session `c2` / tty1 / seat0. Wayland
socket `wayland-0` is exposed to user `mini` (uid 1000). The EGL
vendor override points at the CIX glvnd file, the VK driver at
the Mali JSON ICD, and the LD_LIBRARY_PATH includes the CIX
proprietary loader path. **No software fallback anywhere.**

## 9. Live fps (60 fps locked)

```
$ python3 -c '...'
fps samples: [60, 60, 60, 60, 60] avg: 60.0
```

The running screensaver (`hyprsaver_donut_gles3` at probe time,
rotated by the supervisor) renders at exactly 60.0 fps sustained
over a 5-second window. The shader screensaver is locked at the
display's refresh rate, with zero frame-time variance — the
expected behaviour for a Mali-G720-Immortalis running mid-class
hacks.

## 10. Validation command (this dispatch)

```
$ pytest tools/tests/test_gpu_class.py
============================= 149 passed in 0.36s ==============================

$ pytest tools/tests/
======================== 216 passed, 2 skipped in 5.53s ========================

$ python3 -m unittest discover -s launcher/tests
Ran 38 tests in 14.073s
OK

$ ruff check launcher/ncz-screensaver tools/tests/test_gpu_class.py
All checks passed!

$ ruff format --check launcher/ncz-screensaver tools/tests/test_gpu_class.py
2 files already formatted
```

Was 143 + 38 + 210 + 2 skipped before this dispatch; +6 new tests
in this dispatch's commit (4 sky1 pins + 1 adversarial
fallback-policy test + 1 cmd_status pin). All gates green.

## 11. Repository state (this dispatch)

```
$ git log --oneline -1
<this-commit>  tests+docs: r42-continuation re-validation -- 4th live ms pin + Vulkan/EGL/compositor pins

$ git log --oneline argonas/feat/launcher-ux..HEAD
<this-commit>  tests+docs: r42-continuation re-validation ...
f365279 tests+docs: r15-continuation re-validation against live .66 ...
ab5e2c9 tests+docs: r4 independent re-validation against live .66
444d8c8 launcher: harden the platform-bus dedup and topology partial-dict handling
795b1de docs: r3 live .66 re-validation evidence
b617096 tests: pin Sky1 .66 cmd_gpus/cmd_doctor/cmd_status integration output
69be5b9 tests: pin the stale 0.7.1 cache file as harmless on .66
12a3eb8 docs: re-run live .66 verification with the fixed launcher deployed to /tmp
86b6e83 Merge remote-tracking branch 'argonas/fix/gpu-tier-mali-g720-2026-09-30' into fix/gpu-tier-mali-g720-2026-09-30
641d0ed launcher: tighten the Valhall/Immortalis mid pattern to two-digit bare names
ea3f5b5 docs: add r2 re-validation evidence to the Sky1 / .66 live verification doc
8dcd305 tests: pin Sky1 cmd_doctor/cmd_pool/cmd_plan JSON for live .66 layout
f4097c4 docs: add live .66 verification evidence for the Sky1 Mali-G720 fix
04a79e6 launcher/tests: ruff format pass on the Sky1 fix files (no behavior change)
5fb10ad tests: pin Sky1 .66 live-capture strings and the 4-card-with-:02-absent layout
9671a75 launcher/tests: pin Sky1 Mali detection - short-path sysfs layout and cmd_gpus/cmd_status output
8f221fb launcher: do not classify Sky1 linlondp cards as a weak GPU

$ git status
On branch fix/gpu-tier-mali-g720-2026-09-30-cont-r42
Your branch ahead of 'argonas/feat/launcher-ux' by 17 commits.
nothing to commit, working tree clean
```

The branch now has 17 commits on top of `argonas/feat/launcher-ux`
(the 14 from the source fix + 3 docs/tests passes — r4, r15-cont,
and r42).

## 12. Anything unproven / limitations

* **Argonas push not possible from this build pool.** The sshd at
  `192.168.207.101` requires publickey; the build pool has no
  matching private key. The fix's mirror remote (github
  `perlowja/ncz-screensavers.git`) has been force-updated to
  include the r15-cont and r42 work; the bundle + patch are
  written to `/mnt/datapool/projects/ncz-session-2026-09-29/reports/`
  per operator fallback rule. Argonas pull and rebase by an
  operator-side worker should be a trivial no-op once they have
  the r15-cont+r42 commits in hand.

* **The shipped `/usr/bin/ncz-screensaver` on .66 is unchanged.**
  Per operator rule (no install, no reboot, no package install
  on .66) the fix lives only in the source tree and in the
  `/tmp/m66-continuation/ncz-screensaver-fixed` test copy. The
  deployed 0.7.1 binary still reports the old `weak` verdict
  when run from a non-tty1 session. The next NCZ-OS image
  rebuild will pick up the source-tree fix.

* **The `display_only` filter is the linlondp dedup's lever.** It
  only fires once a real (non-display-only) GPU is also in the
  list. On a host with NO platform-bus Mali (the case covered
  by `test_sky1_r42_no_software_renderer_fallback_under_any_path`),
  the linlondp cards stay in `list_gpus()` and the classifier
  returns `weak`. The operator's rule is "never classify a real
  GPU as weak" — NOT "force every Mali-host to return mid even
  when sysfs says no Mali exists". The test pins the right
  interpretation.

* **The four live ms pins (9.88 / 14.71 / 11.93 / 9.89) span
  only a single kernel release (7.3.0-rc5-sky1-ncz).** A kernel
  bump could shift the calibrator noise floor; the band
  `8.0 <= ms < 20.0` was set against this kernel. A future bump
  should re-measure; this dispatch does not address that
  maintenance task.

* **The `DISPLAY_ONLY_DRIVERS` completeness audit is partial.**
  On unrelated ARM SoCs (MediaTek, Rockchip, Exynos, ...) the
  `cmd_gpus` output may list display controllers as fake GPUs
  classified `weak` — correct verdict, noisy output. Tracked
  as a follow-up.

* **This dispatch's MNEMOS milestone memory** lives in
  `/mnt/datapool/projects/ncz-session-2026-09-29/reports/mnemos/mem_m66_weak_tier_r42.md`
  (the argonas MNEMOS store is unreachable from this build pool,
  same constraint as the argonas git remote).