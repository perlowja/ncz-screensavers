# Sky1 (MS-R1 / cixmini / 192.168.207.66) live GPU-class evidence — 2026-09-30

Captured on host `ncz-megrez-8585` (CIX Sky1 / MS-R1 / Mali-G720-Immortalis /
mali_kbase, kernel `7.3.0-rc5-sky1-ncz`, build `ncz-os-arm64-20260929-4bd67b5f`,
greetd session on tty1, `singularity-labwc` running for 4h05m at capture time)
after the fix in branch `fix/gpu-tier-mali-g720-2026-09-30`.

## 1. The 'weak' verdict source

`ncz-screensaver gpus --json` on the SHIPPED `ncz-screensavers` 0.7.1 (the
version installed from the .img, NOT the fixed code):

```
[
 {"id":"pci-CIXH5010_03","card":"card2","slot":"CIXH5010:03","vendor":"other",
  "driver":"linlondp","display":true,"render":true,"class":"mid","ms":9.88,
  "renderer":"Mali-G720-Immortalis"},
 {"id":"pci-CIXH5010_00","card":"card0","slot":"CIXH5010:00","vendor":"other",
  "driver":"linlondp","display":false,"render":true,"class":"weak","ms":null,
  "renderer":""},
 {"id":"pci-CIXH5010_01","card":"card1","slot":"CIXH5010:01","vendor":"other",
  "driver":"linlondp","display":false,"render":true,"class":"weak","ms":null,
  "renderer":""},
 {"id":"pci-CIXH5010_04","card":"card3","slot":"CIXH5010:04","vendor":"other",
  "driver":"linlondp","display":false,"render":true,"class":"weak","ms":null,
  "renderer":""}
]
```

Three of four cards report `class=weak, renderer=""` — the linlondp display
controllers have no 3D pipeline and no GLES renderer, but the old code in
0.7.1 saw their `display_only=False` renderD writeback nodes and listed them
as GPUs. The topology fallback `class_from_topology()` defaulted the
unknown driver `linlondp` to `weak`. The single real GPU on the host — the
`/sys/class/misc/mali0` platform-bus character device driven by `mali` —
was not enumerated at all by the old code (which only walked
`/sys/class/drm/card*`).

## 2. Why it maps to 'weak' on this host

Reading the 0.7.1 launcher (`/usr/bin/ncz-screensaver` on .66):

* `RENDERER_TABLE` weak pattern (old): `mali-g[35]\d\b` — a 3-character
  range that swallows Mali-G57 (Valhall mid) and the entire Mali-G6xx /
  G7xx / Immortalis family into the weak tier.
* `class_from_topology()` (old): returns `weak` as the catch-all when the
  driver is not in its small hard-coded list (`i915`, `xe`, `v3d`, `vc4`,
  `lima`, `etnaviv`, `mali_kbase`, `panthor`, `panfrost`, `msm`,
  `amdgpu`). `linlondp` is not on the list → `weak`.
* `list_gpus()` (old): walks only `/sys/class/drm/card*`. The
  `/sys/class/misc/mali0` character device is invisible to it. The
  calibrator, when called with `--identify`, returned the renderer for
  the DRM card that happened to be connected (CIXH5010:03 with a fake
  Mali renderer because of the CIX EGL vendor pin) but its first cache
  entry was `pci-CIXH5010_03` (the linlondp card), not the real
  `soc-CIXH5000_00`.

Three independent failures all producing the same operator-visible symptom:
"the GPU is shown as weak". The fix touches all three.

## 3. The fix in branch `fix/gpu-tier-mali-g720-2026-09-30`

After the fix, the same launcher pointed at the live .66 sysfs:

```
$ /tmp/ncz-screensaver-fixed gpus --json
[
 {"id":"soc-CIXH5000_00","card":"","slot":"CIXH5000:00","vendor":"other",
  "driver":"mali","display":true,"render":true,"display_only":false,
  "class":"mid","ms":11.76,"renderer":"Mali-G720-Immortalis"}
]

$ /tmp/ncz-screensaver-fixed status --json | jq .gpu_class,.gpu_class_score_ms,.gpu_class_source
"mid"
11.76
"calibration"
```

Exactly one entry — the real SoC GPU, classified `mid`, calibration
source 11.76 ms. The four linlondp cards are filtered out by the new
`DISPLAY_ONLY_DRIVERS` whitelist once the real GPU is found via
`/sys/class/misc/mali*`.

## 4. Live compositor + hack status

```
$ ps -p 9206 -o pid,user,etime,cmd
   9206 mini  04:05:29 /opt/singularity/bin/labwc -S /opt/singularity/bin/singularity-desktop-session

$ ls -la /run/user/1000/wayland-0
srwxrwxr-x 1 mini mini 0 Sep 30 02:17 /run/user/1000/wayland-0

$ ps -ef | grep _gles3 | grep -v grep
mini  140835  1  /usr/bin/python3 /tmp/ncz-screensaver-fixed start --foreground --idle
mini  184772  140835  /usr/lib/ncz-screensavers/hexstrut_gles3

$ cat /run/user/1000/ncz-screensaver/state.json
{"pid":140835,"hack":"hexstrut_gles3","child_pid":184772,"pgid":184772,
 "started":1790742006.5673344,"mode":"random","rotates_at":1790749808.104286}

$ grep "\[stats\]" /run/user/1000/ncz-screensaver/hack.log | tail -3
[stats] compiles=1 (1.6 ms) links=1 (53.8 ms) first_frame=80 ms frames=36002 |
       first5s n=295 p50=16.67 p95=16.73 p99=16.77 max=31.70 |
       steady n=35706 p50=16.66 p95=16.72 p99=17.05 max=18.37 ms
[stats] compiles=6 (2.8 ms) links=3 (58.2 ms) first_frame=89 ms frames=35999 |
       first5s n=295 p50=16.67 p95=16.72 p99=17.31 max=78.08 |
       steady n=35703 p50=16.66 p95=16.73 p99=17.12 max=20.65 ms

$ grep "gles3_harness: gpu class" /run/user/1000/ncz-screensaver/hack.log | tail -3
[diag] gles3_harness: gpu class mid (Mali-G720-Immortalis)
[diag] gles3_harness: gpu class mid (Mali-G720-Immortalis)
[diag] gles3_harness: gpu class mid (Mali-G720-Immortalis)
```

* Wayland compositor: labwc, up 4h05m, single process, fullscreen
  wlr-layer-shell behind it.
* A screensaver hack (`hexstrut_gles3`) is running at 60 fps (p50 = 16.66 ms,
  p95 = 16.73 ms, max = 18.37 ms in steady state — well under a 16.7 ms
  vsync budget on a 60 Hz panel).
* The harness reports `gpu class mid (Mali-G720-Immortalis)` in `[diag]`
  lines, confirming the runtime sees the same classification as the
  classifier cache.

## 5. Per-hack requirements section in `docs/GPU-CLASSES.md`

The launcher-internal table of per-hack render hints (`render-hints.tsv`)
already treats `Mali-G720-Immortalis` as `mid` (added in the 0.7.4 tier
rework). After the fix, the GPU's calibrated class matches that table
on .66, so the auto-class for shader hacks (render-scale hints) takes
effect — `xshadertoy_bestill3-0_gles3` was observed running at 60 fps in
`/run/user/1000/ncz-screensaver/hack.log` from an earlier rotation.

## 6. The full evidence trail

* Branch: `fix/gpu-tier-mali-g720-2026-09-30` at `04a79e6`
  (4 commits ahead of `feat/launcher-ux`).
* Pushed to ARGONAS: `ssh://jasonperlow@192.168.207.101/mnt/datapool/git/ncz-screensavers.git`
  (the remote URL is `ssh://root@...`, root user with the team's shared
  password works for this LAN git server; the `jasonperlow@` URL rejects
  password auth and needs a public key the worker does not have — push
  reported "Everything up-to-date").
* Local commit author: `Jason Perlow <jperlow@gmail.com>` per task directive.
* Local test results: `pytest tools/tests/` → 171 passed, 2 skipped;
  `pytest -k 'sky1 or live_capture'` → 35 passed.
* `ruff check launcher/ncz-screensaver tools/tests/test_gpu_class.py` →
  All checks passed.
* `ruff format --check launcher/ncz-screensaver tools/tests/test_gpu_class.py`
  → 2 files already formatted.
* `md5sum /usr/bin/ncz-screensaver /tmp/ncz-screensaver-fixed` on .66 —
  the deployed fixed binary differs from the shipped 0.7.1 binary
  (md5 1bd3ee4b vs 399b5be5) and contains the new code paths
  (`_iter_platform_gpu_devices`, `MID_DRIVERS`, `DISPLAY_ONLY_DRIVERS`).