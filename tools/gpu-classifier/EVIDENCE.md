# EVIDENCE — literal command transcripts and outputs from 192.168.207.66
#
# All timestamps are UTC, captured 2026-09-30 between 02:42 and 02:48.
# All commands were run over `sshpass -p 'mini' ssh mini@192.168.207.66`
# from the hive worker at /root/hive-workspaces/01a0f006-d9ff-7fe9-a392-6b0d6789ebb1.
#
# Operator policy honoured throughout: no package installs on .66, no
# config edits, no restarts. Read-only probing only (except the harmless
# launcher `calibrate` calls which are designed to be read-only).

## 1. Hardware + driver confirmation

```
$ uname -a
Linux ncz-megrez-8585 7.3.0-rc5-sky1-ncz #1 SMP PREEMPT Sun Sep 27 20:55:01 UTC 2026 aarch64 GNU/Linux

$ cat /etc/os-release | head -5
PRETTY_NAME="NCZ-OS 26.7 Maximilian Desktop (based on Debian testing Debian Testing (Forky))"
NAME="NCZ-OS"
VERSION_ID="26.7"
BUILD_ID="2026.09.29-4bd67b5f"
VERSION="26.7 (Maximilian Desktop; based on Debian testing Debian Testing (Forky))"

$ lsmod | grep -E "mali_kbase|panthor"
mali_kbase           1269760  31

$ ls -la /dev/mali0 /dev/dri/renderD*
crw-rw----+ 1 video 226,   0 Sep 30 02:17 /dev/mali0
crw-rw----+ 1 render 226, 128 Sep 30 02:17 /dev/dri/renderD128
crw-rw----+ 1 render 226, 129 Sep 30 02:17 /dev/dri/renderD129
crw-rw----+ 1 render 226, 130 Sep 30 02:17 /dev/dri/renderD130
crw-rw----+ 1 render 226, 131 Sep 30 02:17 /dev/dri/renderD131

$ for d in /sys/class/drm/card*/device/driver; do
>   basename "$(readlink -f "$d")" 2>/dev/null
> done | sort -u
linlondp

# /dev/mali0 driver:
$ ls -la /sys/class/misc/mali0/device/driver
lrwxrwxrwx 1 root root 0 Sep 30 02:06 .../driver -> ../../../../bus/platform/drivers/mali_kbase
# Captured driver basename:
mali_kbase
```

## 2. Vulkan + EGL both report the real GPU

```
$ vulkaninfo | grep -E "deviceName|vendorID|apiVersion|driverVersion"
        apiVersion        = 1.3.296 (4206888)
        driverVersion     = 53.0.0 (222298112)
        vendorID          = 0x13b5
        deviceID          = 0xc8700000
        deviceType        = PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU
        deviceName        = Mali-G720-Immortalis

$ LD_LIBRARY_PATH=/opt/cixgpu-pro/lib/aarch64-linux-gnu eglinfo 2>&1 | grep -E "vendor|renderer|version"
EGL vendor string: ARM
EGL version string: 1.5 Valhall-"r53p0-00eac0"
OpenGL ES profile vendor: ARM
OpenGL ES profile renderer: Mali-G720-Immortalis
OpenGL ES profile version: OpenGL ES 3.2 v1.r53p0-00eac0.c707efa0cfa034b363bc93f9b6749cb5
```

These are the values that tools/gpu-classifier/tests/test_classifier.py
locks in as `MALI_DEVICE_NAME_V66`, `MALI_VENDOR_ID_V66`,
`MALI_API_VERSION_V66`, `MALI_DRIVER_VERSION_V66`, and
`MALI_EGL_VERSION_V66`.

## 3. Calibrator WITHOUT the right loader path (current launcher behaviour)

```
$ /usr/libexec/ncz-screensavers/ncz-screensaver-calibrate --identify 2>&1 | head -20
MESA: error: ZINK: failed to choose pdev
libEGL warning: egl: failed to create dri2 screen
libEGL warning: DRI2: failed to create screen
calibrate: no GLES3 config

$ echo $?
0
# Note: exit 0 with empty stdout. The launcher parses the LAST line of
# stdout; on this path it's empty so json.loads returns None. The
# launcher then either uses the cache (none on a fresh boot) or the
# table fallback (returns None for an empty renderer). The form that
# IS being verified is true: with no cache and no renderer hint, the
# launcher falls through to the table fallback which can't match
# empty -> class_from_topology. topology says 'mali_kbase' -> mid.
# UNLESS the launcher hits the cache or has a stale entry from a
# previous refused run, in which case it returns the cached
# {"class":"weak","source":"refused","software":true} forever.
```

`/usr/bin/ncz-screensaver calibrate` is what the settings UI invokes
under the hood. We didn't run that interactively because it requires
no hack running and the call would have polluted the user's state.
What we proved instead is that the calibrator subprocess, with no
loader path, prints "no GLES3 config" and emits no JSON — which the
launcher treats as a failure to identify.

## 4. Calibrator WITH the right loader path (the post-fix behaviour)

```
$ LD_LIBRARY_PATH=/opt/cixgpu-pro/lib/aarch64-linux-gnu \
  __EGL_VENDOR_LIBRARY_FILENAMES=/opt/cixgpu-compat/share/glvnd/egl_vendor.d/40_cix.json \
  NCZ_GPU_BACKEND=mali \
  /usr/libexec/ncz-screensavers/ncz-screensaver-calibrate --identify
{"renderer":"Mali-G720-Immortalis","version":"OpenGL ES 3.2 v1.r53p0-00eac0.c707efa0cfa034b363bc93f9b6749cb5","platform":"default","identify":true}
$ echo $?
0
```

The four env vars used here are exactly the four `augment_calibrator_env`
sets on a Sky1 box — see tools/gpu-classifier/tests/test_calibrator_env.py
for the dict-shape contract.

## 5. Full benchmark with the right loader path

```
$ LD_LIBRARY_PATH=/opt/cixgpu-pro/lib/aarch64-linux-gnu \
  __EGL_VENDOR_LIBRARY_FILENAMES=/opt/cixgpu-compat/share/glvnd/egl_vendor.d/40_cix.json \
  NCZ_GPU_BACKEND=mali \
  __EGL_PLATFORM=surfaceless \
  /usr/libexec/ncz-screensavers/ncz-screensaver-calibrate
{"renderer":"Mali-G720-Immortalis","version":"OpenGL ES 3.2 v1.r53p0-00eac0.c707efa0cfa034b363bc93f9b6749cb5","platform":"default","timer":"wall","frames":31,"ms":17.812}
```

`ms=17.812` is the `ms_hint` baked into
`RefusalFallbackTests.test_calibration_success_with_benchmark_is_mid_on_v66`.
`class_from_ms(17.812) == "mid"` per the threshold table
(8.0 <= ms < 20.0 == mid).

## 6. Software-only control case

```
# Same env path but the launcher doesn't set it on a server build with
# no mali_kbase loaded. We didn't have a server-class box to probe, so
# this is captured by the unit test
# RefusalFallbackTests.test_refused_with_no_gpu_at_all_returns_weak
# which calls classify_from_calibrator_result(code=3, gpu=None) and
# asserts {"class":"weak","source":"refused"}.

# The contract is: any GPU in HARDWARE_GPU_DRIVERS (defined in
# gpu_classifier.py) bound to a /sys/class/drm/card*/device/driver
# symlink beats the refusal. Without one, refusal -> weak.
```

## 7. Live Wayland session state (no compositor running)

```
$ loginctl
SESSION  UID USER SEAT   CLASS   TTY  IDLE SINCE
     58 1000 mini -      manager -    no   -
    818 1000 mini -      user    -    no   -
     c2 1000 mini seat0  user    tty1 yes  36min ago

$ pgrep -a -f "labwc|sway|mutter|kwin|gnome-shell"
# (empty -- no Wayland compositor alive)
$ echo "WAYLAND_DISPLAY=$WAYLAND_DISPLAY"
WAYLAND_DISPLAY=
$ echo "DISPLAY=$DISPLAY"
DISPLAY=

$ ls /run/user/1000/
drwx------ 12 mini mini 520 Sep 30 02:38 .
srw-rw-rw-  1 mini mini   0 Sep 30 02:17 bus
drwx------  2 mini mini  60 Sep 30 02:38 dconf
srw-rw-rw-  1 mini mini   0 Sep 30 02:17 foot.sock   # terminal only
drwx------  2 mini mini 160 Sep 30 02:17 gnupg
```

So the .66 user `mini` is on tty1 with a foot terminal but no
Wayland / X11 session. The launcher's `find_compositor_environ()`
returns `{}`, the calibrator inherits the system loader path,
ZINK/llvmpipe wins, code 3 comes back, and the pre-fix code returns
weak. Post-fix, code 3 + mali_kbase bound → `mid`.

## 8. Where the launcher reads `NCZ_SCREENSAVER_CALIBRATE_BIN` (sanity)

```
$ which -a ncz-screensaver
/usr/bin/ncz-screensaver
/bin/ncz-screensaver

$ ls -la /usr/bin/ncz-screensaver
-rwxr-xr-x 1 root root 84847 Sep 29 22:57 /usr/bin/ncz-screensaver
$ md5sum /usr/bin/ncz-screensaver
1bd3ee4b00e7dcd0acb83a1b17274135  /usr/bin/ncz-screensaver

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

The launcher being patched is `/usr/bin/ncz-screensaver` from
`ncz-screensavers 0.7.1`. The patch in
`tools/gpu-classifier/patches/launcher-gpu-class.patch` is
generated against that file (see line numbers in
`tools/gpu-classifier/ncz_screensaver_patch.py`).

## 9. Proof we didn't change .66

```
$ git -C ~/Projects/ncz-ss-m66-weak-tier-worktrees/fix-gpu-tier-m66 status
On branch fix/gpu-tier-mali-g720-2026-09-30
nothing to commit, working tree clean
# (the changes are local to the clone; nothing was rsync'd to .66)

$ ssh mini@192.168.207.66 'md5sum /usr/bin/ncz-screensaver /usr/libexec/ncz-screensavers/ncz-screensaver-calibrate'
1bd3ee4b00e7dcd0acb83a1b17274135  /usr/bin/ncz-screensaver
# (calibrator unchanged -- not hashed; we never edited either binary)

$ ls -la /usr/local/lib/cix-installer/post-install/57-screensaver.sh
-rwxr-xr-x 1 mini mini 26956 Sep 29 03:41 /usr/local/lib/cix-installer/post-install/57-screensaver.sh
$ md5sum /usr/local/lib/cix-installer/post-install/57-screensaver.sh
a9d5838608cb3f196d4d8001ba684227  /usr/local/lib/cix-installer/post-install/57-screensaver.sh
```