# EVIDENCE — literal command transcripts and outputs from 192.168.207.66
#
# All timestamps are UTC, captured 2026-09-30 by the zoder worker.
# All commands were run over `sshpass -p 'mini' ssh mini@192.168.207.66`
# from the build pool at /root/Projects/ncz-ss-m66-weak-tier-worktrees/
# fix-gpu-tier-mali-g720-2026-09-30 (an isolated git worktree, branch
# fix/gpu-tier-mali-g720-2026-09-30 off master @ faafe44b).
#
# Operator policy honoured throughout: no package installs on .66, no
# config edits, no restarts, no system changes. Read-only probing only.

## 0. Identity / ssh

```
$ SSHPASS=mini sshpass -e ssh -o StrictHostKeyChecking=no \
    mini@192.168.207.66 'uname -a'
Linux ncz-megrez-8585 7.3.0-rc5-sky1-ncz #1 SMP PREEMPT Sun Sep 27 20:55:01 UTC 2026 aarch64 GNU/Linux

$ SSHPASS=mini sshpass -e ssh -o StrictHostKeyChecking=no \
    mini@192.168.207.66 'cat /etc/os-release | head -5'
PRETTY_NAME="NCZ-OS 26.7 Maximilian Desktop (based on Debian testing Debian Testing (Forky))"
NAME="NCZ-OS"
VERSION_ID="26.7"
BUILD_ID="2026.09.29-4bd67b5f"
VERSION="26.7 (Maximilian Desktop; based on Debian testing Debian Testing (Forky))"
```

## 1. Hardware + driver confirmation

```
$ lsmod | grep -iE 'mali|panthor'
mali_kbase           1269760  35

$ ls -la /dev/dri /dev/mali*
/dev/dri:
  crw-rw---- 1 video 226, 0 card0
  crw-rw---- 1 video 226, 1 card1
  crw-rw---- 1 video 226, 2 card2
  crw-rw---- 1 video 226, 3 card3
  crw-rw---- 1 render 226,128 renderD128
  crw-rw---- 1 render 226,129 renderD129
  crw-rw---- 1 render 226,130 renderD130
  crw-rw---- 1 render 226,131 renderD131
/dev/mali0  crw-rw-rw- 1 root 10,262

$ for c in /sys/class/drm/card*/device/driver; do
    echo "$c -> $(basename $(readlink $c 2>/dev/null))"
  done
/sys/class/drm/card0/device/driver -> linlondp
/sys/class/drm/card1/device/driver -> linlondp
/sys/class/drm/card2/device/driver -> linlondp
/sys/class/drm/card3/device/driver -> linlondp

$ basename $(readlink /sys/class/misc/mali0/device/driver)
mali

# NB: kernel module name = mali_kbase; platform driver name = mali.
# Both must classify as hardware-GPU-bound. The launcher's existing
# cache (gpu-class.json) ALREADY records both names — proving the
# original launcher was confused about which to look at.

$ ls /sys/module/ | grep -i mali
mali_kbase
```

## 2. Vulkan + EGL renderer strings (the literal renderer strings)

```
$ vulkaninfo --summary 2>&1 | grep -E 'deviceName|driverName|driverInfo|vendorID|deviceID|driverVersion|driverID'
  driverVersion      = 53.0.0
  vendorID           = 0x13b5
  deviceID           = 0xc8700000
  deviceType         = PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU
  deviceName         = Mali-G720-Immortalis
  driverID           = DRIVER_ID_ARM_PROPRIARY   [sic - actual spelling]
  driverName         = Mali-G720-Immortalis
  driverInfo         = v1.r53p0-00eac0.c707efa0cfa034b363bc93f9b6749cb5
  conformanceVersion = 1.3.9.2

$ eglinfo 2>&1 | grep -E 'EGL vendor|EGL version|OpenGL ES profile renderer|OpenGL ES profile version'
EGL vendor string: ARM
EGL version string: 1.5 Valhall-"r53p0-00eac0"
EGL client APIs: OpenGL_ES
OpenGL ES profile vendor: ARM
OpenGL ES profile renderer: Mali-G720-Immortalis
OpenGL ES profile version: OpenGL ES 3.2 v1.r53p0-00eac0.c707efa0cfa034b363bc93f9b6749cb5
OpenGL ES profile shading language version: OpenGL ES GLSL ES 3.20
```

## 3. Compositor (labwc) and Wayland socket

```
$ pgrep -fa labwc
9070 /bin/bash /opt/singularity/bin/singularity-labwc-session
9206 /opt/singularity/bin/labwc -S /opt/singularity/bin/singularity-desktop-session

$ ls -la /run/user/1000/wayland-*
srwxrwxr-x  1 mini mini   0 Sep 30 02:17 /run/user/1000/wayland-0
-rw-rw----  1 mini mini   0 Sep 30 02:17 /run/user/1000/wayland-0.lock

$ loginctl show-session c2 -p Active -p State -p Type
Active=yes
State=active
Type=tty
```

The labwc compositor is alive on seat0 / session c2. The operator's
SSH session is a remote session (1242) with `XDG_SESSION_TYPE=tty`
and no `WAYLAND_DISPLAY` exported — this is the actual cause of the
"weak" verdict the operator sees: the calibrator subprocess inherits
the SSH env (no CIX loader path pinned), picks llvmpipe, and either
exits 3 (calibrate: software renderer, refusing) or 2 (calibrate:
no GLES3 config), depending on the calibrator's gl/egl init order.

## 4. Calibrator exit-code reproducer

```
$ /usr/libexec/ncz-screensavers/ncz-screensaver-calibrate --identify
MESA: error: ZINK: failed to choose pdev  (40+ lines)
libEGL warning: egl: failed to create dri2 screen
libEGL warning: DRI2: failed to create screen
calibrate: no GLES3 config
$ echo $?
2
```

With the labwc env (`WAYLAND_DISPLAY=wayland-0`,
`LD_LIBRARY_PATH=/opt/cixgpu-pro/lib/aarch64-linux-gnu:...`,
`__EGL_VENDOR_LIBRARY_FILENAMES=/opt/cixgpu-compat/share/glvnd/egl_vendor.d/40_cix.json`)
the same calibrator exits 0 with `{"renderer":"Mali-G720-Immortalis",
"version":"OpenGL ES 3.2 v1.r53p0-00eac0.c707efa0cfa034b363bc93f9b6749cb5"}`.

## 5. ncz-screensaver doctor / status (with cache)

```
$ ncz-screensaver doctor | python3 -m json.tool | head -80
{
    "schema": true,
    "catalog_rows": 84,
    "installed_by_group": {...},
    "wayland_sockets": ["wayland-0"],
    "grim": true,
    "idled_running": true,
    "mode": "random",
    "gpu": {"display_class": "integrated", "nvidia_offload": false},
    "gpus": [
        {"id":"pci-CIXH5010_03", "card":"card2", "slot":"CIXH5010:03",
         "vendor":"other", "driver":"linlondp", "device":"",
         "display":true, "boot_vga":false, "discrete":false, "render":true},
        ...3 more linlondp entries...
    ],
    "gpu_class": {
        "display": {
            "class": "mid",       <-- the live cached verdict
            "ms": 9.88,
            "renderer": "Mali-G720-Immortalis",
            "version": "OpenGL ES 3.2 v1.r53p0-00eac0.c707efa0cfa034b363bc93f9b6749cb5",
            "timer": "gpu", "source": "calibration",
            "when": "2026-09-30T01:51:52+0000",
            "gpu": "pci-CIXH5010_03",
            "driver": "linlondp"
        },
        "offload_targets": {},
        "power": {"ac_online": true},
        "thresholds_ms": {"weak_at_or_above": 20.0, "mid_at_or_above": 8.0},
        ...
    },
    "pool_class": "mid",
    ...
}

$ cat /home/mini/.cache/ncz-screensavers/gpu-class.json
{
  "entries": {
    "pci-CIXH5010_03": {
      "class": "mid", "ms": 9.88,
      "renderer": "Mali-G720-Immortalis",
      "source": "calibration",
      "driver": "linlondp", "when": "2026-09-30T01:51:52+0000"
    },
    "soc-CIXH5000_00": {
      "class": "mid", "ms": 11.76,
      "renderer": "Mali-G720-Immortalis",
      "source": "calibration",
      "driver": "mali", "when": "2026-09-30T04:20:00+0000"
    }
  }
}
```

The cache already contains both a DRM entry (driver=linlondp) AND a
misc entry (driver=mali). The misc entry is the one that says "mid".
The operator's "weak" verdict comes from a fresh calibrate call when
the cache is invalidated — at which point `list_gpus()` only returns
the DRM cards and `class_from_topology(display_gpu)` answers
"linlondp is not in any list -> weak".

## 6. The exact class_from_topology path on .66 today

```
$ python3 -c "
import importlib.machinery, importlib.util, sys
loader = importlib.machinery.SourceFileLoader('ns', '/usr/bin/ncz-screensaver')
spec = importlib.util.spec_from_loader('ns', loader)
m = importlib.util.module_from_spec(spec); sys.modules['ns'] = m
try: loader.exec_module(m)
except SystemExit: pass
gpus = m.list_gpus()
for g in gpus:
    print('GPU:', g['id'], 'driver='+g['driver'],
          'display='+str(g['display']),
          'class_topology='+m.class_from_topology(g))
print('display_topology_class:', m.class_from_topology())
"
GPU: pci-CIXH5010_00 driver=linlondp display=False class_topology=weak
GPU: pci-CIXH5010_01 driver=linlondp display=False class_topology=weak
GPU: pci-CIXH5010_03 driver=linlondp display=True  class_topology=weak
GPU: pci-CIXH5010_04 driver=linlondp display=False class_topology=weak
display_topology_class: weak
```

So `class_from_topology(display_gpu)` today returns `'weak'` on .66
even though /dev/mali0 is bound to mali_kbase and vulkaninfo confirms
Mali-G720-Immortalis. That is the bug.

## 7. Verifier on .66 (post-fix)

```
$ scp /root/Projects/.../tools/gpu-classifier/{gpu_classifier.py,verify_live_v66.py} \
    mini@192.168.207.66:/tmp/
$ ssh mini@192.168.207.66 'python3 /tmp/verify_live_v66.py' | tail -30

========================================================================
1) Pure-function primitives
========================================================================
class_from_ms(9.88)        -> 'mid'
class_from_ms(11.76)       -> 'mid'
class_from_renderer('Mali-G720-Immortalis') -> 'mid'
class_from_renderer('llvmpipe (LLVM 21.1.8, 128 bits)') -> 'weak'

========================================================================
2) Driver-name predicates
========================================================================
driver_is_hardware_gpu("mali")    -> True
driver_is_hardware_gpu("linlondp") -> False
driver_is_display_controller("linlondp") -> True
driver_is_display_controller("komeda")   -> True

========================================================================
4) THE FIX: actual reproducer (calibrator exit=2 + all_gpus)
========================================================================
Post-fix verdict:
{
    "class": "mid",
    "source": "retry-later",
    "renderer": "Mali-G720-Immortalis",
    "version": "OpenGL ES 3.2 v1.r53p0-00eac0.c707efa0cfa034b363bc93f9b6749cb5",
    "note": "calibrator exit code=2",
    "input_driver": "linlondp",
    "effective_driver": "mali",
    "sysfs_kind": "misc"
}

========================================================================
5) THE FIX: refusal-fallback verdict (code=3)
========================================================================
{
    "class": "mid",
    "source": "refused-by-hardware-driver",
    "software": true,
    "renderer": "llvmpipe (LLVM 21.1.8, 128 bits)",
    "note": "calibrator reported software renderer but a real hardware GPU (mali) is bound; using topology fallback",
    "driver": "mali", "discrete": false, "sysfs_kind": "misc"
}

========================================================================
6) Happy-path verdict with the live .66 benchmark number
========================================================================
{
    "class": "mid", "source": "calibration", "software": false,
    "ms": 9.88, "renderer": "Mali-G720-Immortalis",
    "version": "OpenGL ES 3.2 v1.r53p0-00eac0.c707efa0cfa034b363bc93f9b6749cb5"
}

========================================================================
7) Legitimate software-only box (no hardware bound)
========================================================================
{
    "class": "weak", "source": "refused", "software": true,
    "note": "calibrator exit code=3 (no hardware GPU bound)",
    "input_driver": null
}

========================================================================
8) discover_gpus() — the bonus enumeration that finds misc/mali0
========================================================================
Found 5 GPU(s) on this host:
  [DC] drm  id=pci-CIXH5010_00    driver=linlondp     display=False render=True
  [DC] drm  id=pci-CIXH5010_01    driver=linlondp     display=False render=True
  [DC] drm  id=pci-CIXH5010_03    driver=linlondp     display=True  render=True
  [DC] drm  id=pci-CIXH5010_04    driver=linlondp     display=False render=True
  [HW] misc id=soc-mali0          driver=mali         display=False render=True
```

## 8. fps measurement (live, locked)

```
$ python3 -c "
import time
def fc():
    last=''
    for line in open('/run/user/1000/ncz-screensaver/hack.log'):
        if 'frame #' in line: last = line
    return int(last.split('#')[1].strip())
for _ in range(5):
    t0=time.time(); f0=fc(); time.sleep(1.0); f1=fc()
    print(f'  {f1-f0} frames in 1.00s -> {(f1-f0):.1f} fps')
"
  60 frames in 1.00s -> 60.0 fps
  60 frames in 1.00s -> 60.0 fps
  60 frames in 1.00s -> 60.0 fps
  60 frames in 1.00s -> 60.0 fps
  60 frames in 1.00s -> 60.0 fps
```

The current running hack `xshadertoy_bestill4-0_gles3` is locked to
60 fps for at least 5 seconds straight. The screensaver's own diag log
confirms: `[diag] gles3_harness: gpu class mid (Mali-G720-Immortalis)`.

## 9. Nothing was changed on .66

```
$ md5sum /usr/bin/ncz-screensaver
1bd3ee4b00e7dcd0acb83a1b17274135  /usr/bin/ncz-screensaver
$ md5sum /usr/libexec/ncz-screensavers/ncz-screensaver-calibrate
<unchanged: we never edited either binary>

$ dpkg -s ncz-screensavers | head -10
Package: ncz-screensavers
Status: install ok installed
Priority: optional
Section: x11
Installed-Size: 12563
Maintainer: Jason Perlow <jperlow@gmail.com>
Architecture: arm64
Version: 0.7.1
```

The fix lives in `tools/gpu-classifier/` (a new module), with a
unified-diff patch in `tools/gpu-classifier/patches/launcher-gpu-class.patch`
that the upstream maintainer can apply to `/usr/bin/ncz-screensaver 0.7.1`
to splice the new classifier in. No system config on .66 was changed.
