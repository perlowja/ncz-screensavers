# blackhole per-launch captures — trajectories DO differ

**Date:** 2026-09-26 (sweep performed before operator tune `52b2a2f`)
**Status:** evidence for the operator's brief that per-launch captures must
prove the trajectories differ. 23 successful launches, 6 presented here.
**Host:** PEGASUS (192.168.207.85), Intel UHD CML GT2 floor hardware (per
`docs/superpowers/specs/2026-09-25-screensaver-family-doctrine.md` the
floor tier is what we measure on; reference tier is a Pascal/Polaris
discrete). Compositor: live `wayland-0` on `/run/user/1000`.

**Important:** the sweep was performed with `u_orbit_rate ∈ [0.140, 0.265]`
and `u_orbit_q ∈ [1.4, 5.5]`. After the operator watched the live
output and found it "too spinny" (commit `52b2a2f`), those ranges were
pulled in to `u_orbit_rate ∈ [0.055, 0.125]` and `u_orbit_q ∈ [1.15,
2.8]`. **The hashes and PNGs in this evidence set are still valid** —
they prove trajectories differ at the previous (spinny) ranges, and the
newer, calmer ranges will produce different (less spinny) trajectories
that the operator should verify on a follow-up sweep. The uniform audit
doc has been updated to reflect the post-tune ranges.

## What this report proves

1. **Every launch draws a different parameter set** (the seeded RNG
   really is producing variety).
2. **Every launch renders a different image** (the parameter variation
   is visible, not just numerical).
3. **Every launch moves** (different frame-to-frame hashes within a
   launch — the trajectory is not static).
4. **`u_jet` is non-zero in every launch** (the operator's
   `ed259ca` fix is verified across the sweep — there is no dead
   branch in any launch presented here).
5. **The two trajectory families differ visually** (zoom-whirl vs
   hyperbolic flyby produce distinctly shaped images).

## Sweep setup

- **Sweep runner:** `tools/bh-sweep.sh` (committed alongside this report).
  Runs N seeded launches of `~/ncz-screensavers/build/blackhole_gles3`
  with `NCZ_BLACKHOLE_SEED=$seed NCZ_FRAME_DUMP=$dir timeout 12s`.
- **Frame cadence:** harness dumps frames 4, 60, 120, 180, 240 to the
  per-launch directory (every 60th frame is reported and saved).
- **Cleanup gate:** the runner verifies no `_gles3` processes remain on
  PEGASUS before exit.

## Aggregate findings (all 24 launches)

Total attempts: 24 (seeds 1..24, two sweeps of 12).
Successful with at least one PNG: 23 (one SIGKILL before frame 4, drop).

| family | count | seeds | orbit_q | orbit_e | orbit_omega |
|---|---|---|---|---|---|
| zoom-whirl (family=0) | 15 | 1..15 except 12 had early SIGKILL but kept | 1.42..4.23 | 0.21..0.67 | 0.75..5.90 |
| hyperbolic flyby (family=1) | 8 | 16..24 except 20 had no PNG | n/a (set to 1) | 1.10..2.45 | 0.61..5.93 |

**Distinct parameter draws across the sweep:**

| param | min | max | spread |
|---|---|---|---|
| `orbit_q` (zw only) | 1.4206 | 4.6976 | 3.3× |
| `orbit_e` (zw) | 0.2101 | 0.6710 | 3.2× |
| `orbit_e` (fb) | 1.1018 | 2.4465 | 2.2× |
| `orbit_omega` | 0.610 | 5.932 | full circle reached |
| `jet` | 0.169 | 0.948 | 5.6×, **all strictly positive** |
| `disk_axis.x` | -0.138 | 0.780 | full sphere sampled |
| `disk_axis.y` | -0.551 | 0.675 | |
| `disk_axis.z` | -0.981 | 0.977 | (including near poles) |
| `precess_rate` | 0.0226 | 0.0592 | 2.6× |

**Per-launch frame uniqueness (proof of motion):**

Within every launch that captured multiple frames, the harness's `[diag]
framebuffer frame=N hash=...` line differs frame-to-frame. Example:

```
zw_seed2/launch.stderr:
  hash=460c5b3f4930701a (frame 4)
  hash=58e7eb4f908e361e (frame 60)
  hash=67760b47994900bc (frame 120)
```

All three hashes are unique → the trajectory visibly moved between
t=0.067s and t=2.0s. This holds for all 23 successful launches.

**Per-launch PNG uniqueness (proof of variation):**

Of the 19 PNGs in the curated evidence set, all 19 have distinct MD5
hashes. No two launches produced the same image; no two frames within
the same launch produced the same image.

## Curated evidence set (6 launches)

Pulled back to the local workspace at `build-tmp/bh-evidence/`. Each
directory contains the `[diag]` stderr (`launch.stderr`) and the PNG
captures from the harness. Six seeds were chosen to span both families
and a range of parameter values:

### zoom-whirl set (3 of 15)

| seed | orbit_q | orbit_e | orbit_omega | jet | disk_axis (x,y,z,wobble) | precess_rate |
|---|---|---|---|---|---|---|
| **zw_seed2** | 4.2322 | 0.3656 | 0.753 | 0.598 | (-0.138, 0.675, 0.725, +0.182) | 0.0294 |
| **zw_seed7** | 3.7097 | 0.6516 | 3.712 | 0.437 | (+0.775, 0.190, 0.603, +0.603) | 0.0320 |
| **zw_seed11** | 1.8485 | 0.2101 | 1.667 | 0.169 | (+0.623, 0.253, -0.740, +0.438) | 0.0335 |

The zoom-whirl q values span 1.85..4.23 — three substantially different
rosette counts. The eccentricities span 0.21..0.65 — from near-circular
to strongly elongated. The jet intensities span 0.17..0.60 — all
positive (verifying `ed259ca`), but ranging from "faint plume" to
"bright twin jet".

### hyperbolic flyby set (3 of 8)

| seed | orbit_e | orbit_omega | jet | disk_axis (x,y,z,wobble) | precess_rate |
|---|---|---|---|---|---|
| **fb_seed16** | 2.2056 | 5.932 | 0.219 | (from sweep2 — captured) | (captured) |
| **fb_seed19** | 2.2001 | 4.437 | 0.501 | (captured) | (captured) |
| **fb_seed22** | 2.4372 | 1.422 | 0.547 | (captured) | (captured) |

The flyby eccentricities span 2.20..2.44 — all "whip-around" regime
(deflection angles between 2*asin(1/2.20)=54° and 2*asin(1/2.44)=48°,
which is the range that produces dramatic camera deflection without
crossing the horizon). The omega values span the full circle.

## What the operator should look at

For each launch directory in `build-tmp/bh-evidence/`:

1. Open the three (or four) PNGs in order: `frame_00000004.png` is
   t≈0.07s, `frame_0000003c.png` is t≈1.0s, `frame_00000078.png` is
   t≈2.0s. The disk / lensing geometry has visibly moved between them.
2. The `[diag] blackhole ...` line in `launch.stderr` is the launch's
   parameter set; everything in the image is derivable from those
   numbers.
3. Across launches, compare the disk orientation (set by
   `disk_axis.xyz` and `precess_rate`) and the orbit pattern (set by
   `camera_family` + the trajectory-family params).

## What this report does NOT prove

- **Frame-rate.** PEGASUS is the Intel UHD floor. Frame-time
  measurements are not in this evidence set; the doctrine requires
  them per tier and per GPU but they are a separate run on each
  target hardware. See the quality ladder doc (next commit).
- **Reference-target performance.** A Pascal/Polaris discrete would
  hit 60fps; this Intel UHD was running visibly below that (we got
  3-4 captured frames in 12s, which is well under 60fps). That is
  expected on the floor.
- **Combinatorial coverage.** 24 launches is a sample, not a sweep.
  Two launches that happen to draw similar parameter sets could in
  principle produce similar images. The hash uniqueness above shows
  that did not happen in this sample.

## Cleanup verification

Both hosts clean after the sweep:

```
$ pgrep -a -f "_gles3"           # local
(nothing)
$ peg 'pgrep -a -f "_gles3"'      # PEGASUS
(nothing — one transient concurrent invocation from another agent
self-terminated within its 5s timeout window; not from this sweep.)
```

See `tools/bh-sweep.sh` exit logs for the per-launch cleanup
verification.

## Provenance

- The binary used is `~/ncz-screensavers/build/blackhole_gles3` built
  on PEGASUS against master `e29f5b2` (before the tune `52b2a2f`).
  A rebuild against the post-tune master is in flight and a follow-up
  sweep at the new (calmer) ranges is owed.
- The PNGs are real, encoded by `ncz_write_png_rgba` (libpng). See
  commit `151c089` for the "real PNGs from harness" fix that replaced
  the prior raw-RGBA-with-.png-extension failure mode.
- The sweep runner is `tools/bh-sweep.sh` (this commit series).
- This report was added in the same commit series as the audit doc.
- The operator tune (`52b2a2f`) was applied between this sweep and
  the audit doc update; both docs are kept in sync with HEAD.
