# wayshade fidelity tiers - design

Status: DRAFT for operator review. Supersedes nothing; this is the first
written form of the tier design discussed 2026-09-26/27.

## The problem, in measured numbers

The catalogue's cost spread across GPUs is not a gentle gradient, it is two
orders of magnitude. Measured at 1920x1080, frames rendered in 10s:

| piece | Intel UHD CML GT2 (iGPU) | NVIDIA RTX 2060 Mobile |
|---|---|---|
| cellmosaic | 600 (vsync cap, 60fps) | 600 (vsync cap) |
| hexlattice | 600 (vsync cap, 60fps) | 600 (vsync cap) |
| blackhole | 85 (8.5fps) | 560 (56fps) |
| ridgeline | 4 (0.4fps) | 300 (30fps) |

Both machines are the SAME machine: PEGASUS, a hybrid laptop whose panel is
driven by the Intel iGPU with the NVIDIA part available only via offload.

Two conclusions follow, and they set the whole design:

1. A single fixed quality level cannot serve this catalogue. ridgeline is
   150x more expensive than its neighbours on the same silicon.
2. The iGPU is the target, not the fallback. On a hybrid laptop the iGPU owns
   the panel; the discrete GPU is a per-command offload, and offloading a
   screensaver to it is a power decision, not a free win.

ridgeline itself has already been cut from the shipped catalogue (it stays in
the tree as the upper calibration anchor precisely because of that 150x).

## Tiers

Four tiers, as requested: `low`, `medium`, `high`, `extra`.

A tier is primarily an **internal render scale** - render to an offscreen
target at a fraction of output resolution and let the compositor upscale.
This is wayshade Phase 1 item 2 (`wp_viewporter`), and it is the single
highest-leverage knob because almost every shader here is fill-rate bound:
cost falls with the square of the scale factor.

| tier | render scale | pixels vs native | intended floor |
|---|---|---|---|
| low | 0.375 | 14% | 2016-era Gen9 iGPU |
| medium | 0.50 | 25% | modern iGPU |
| high | 0.71 | 50% | strong iGPU / entry dGPU |
| extra | 1.00 | 100% | discrete |

Scale is the mechanism that works for EVERY shader without the shader
knowing. A second, optional mechanism is an `iQuality` uniform (0..3) that a
shader MAY consult to drop its own inner work - octave counts, raymarch step
ceilings, sample counts. That is opt-in per shader and only where it earns
its complexity; most shaders should need nothing but the scale change.

**Do not let a tier change the composition.** A piece at `low` must look like
the same artwork, softer - not a different artwork with fewer elements.

## Detection: a starting tier, not a verdict

The critical design decision: **static hardware classification chooses only
the STARTING tier. Measured frame time governs from then on.**

This is deliberate. Hardware identification is unreliable in ways we have
already measured:

* `switcherooctl` on PEGASUS reports `Discrete: no` for an RTX 2060 Mobile,
  while correctly emitting its NVIDIA offload environment. A vendor tool's
  own discreteness flag is not trustworthy.
* `clinfo` silently omits PEGASUS's Intel iGPU entirely (no Intel OpenCL ICD
  installed) while sysfs enumerates both devices. Corroboration tools see a
  subset of reality.

So: classification gets us into the right neighbourhood quickly, and the
measurement loop corrects it. A machine we mis-detect renders one or two
seconds at the wrong tier and settles. A machine we cannot detect at all
starts at `medium` and settles just the same.

### Platform adapters

Enumeration is platform-specific; the result is not. Each adapter returns the
same `GpuInfo { vendor, device_id, driver, integrated: bool, renderer_string }`.

| platform | primary enumeration | corroboration |
|---|---|---|
| Linux/Wayland | `/sys/class/drm/card*/device/{vendor,device,driver}` | `GL_RENDERER`, optionally `clinfo`/`vulkaninfo` |
| Windows | DXGI `IDXGIFactory::EnumAdapters1` -> `DXGI_ADAPTER_DESC1` (VendorId, DeviceId, DedicatedVideoMemory) | `GL_RENDERER` |
| macOS | Metal `MTLCopyAllDevices` -> `MTLDevice.isLowPower`, `.hasUnifiedMemory`, `.registryID` | `GL_RENDERER` |

macOS note: Apple Silicon has no discrete GPU and unified memory, so
`isLowPower` is the wrong axis there - an M-series GPU is not "low power" in
the sense an Intel iGPU is. Treat Apple Silicon as its own class rather than
forcing it into integrated/discrete. The exact floor for Apple Silicon is
UNKNOWN until measured; do not guess it in code.

**`GL_RENDERER` is the universal fallback** and works on all three platforms.
When an adapter fails or returns nothing recognisable, the engine must still
start - at `medium`.

### Starting-tier rules

```
discrete GPU present AND driving the surface   -> extra
Apple Silicon                                  -> high   (provisional, unmeasured)
integrated, 2020 or newer                      -> high
integrated, 2016-2019 (Gen9 class)             -> medium
integrated, pre-2016 or unidentified           -> low
software rasteriser (llvmpipe/swiftshader/WARP) -> low, and consider not
                                                   running at all
```

The 2016 Gen9 floor is the agreed oldest hardware we care about.

## The measurement loop - this is the real mechanism

Per frame, track a rolling median frame time (median, not mean: a single
compositor hitch must not demote a tier).

```
if median frame time > 1.30 * target for 60 consecutive frames  -> tier down
if median frame time < 0.55 * target for 600 consecutive frames -> tier up, once
```

Asymmetric on purpose. Demote fast because the user is watching something
stutter. Promote slowly and grudgingly, and never oscillate: once a tier has
been demoted and re-promoted at the same resolution, pin it.

Target frame time comes from the compositor's reported refresh interval, not
a hardcoded 60fps.

Timing must come from Wayland presentation feedback where available rather
than `clock_gettime` around the swap - today's harness uses
`CLOCK_MONOTONIC`, which measures our own loop rather than actual present.

## Offload is not a tier

Running on the discrete GPU is NOT `extra`. They are independent axes: which
GPU renders, and how much it renders.

Standing rule, already documented and measured: PRIME offload is PER COMMAND
via `switcherooctl launch`, and the offload environment variables are NEVER
set ambiently - doing so breaks the compositor's own EGL and silently drops
the whole desktop to software rendering. wayshade must therefore never set
them in-process for itself.

A screensaver that only reaches target by waking a discrete GPU is a battery
decision the user should make, not a default. Default remains: render on the
GPU that owns the panel, and pick a tier that fits it.

## What this does NOT do

* It does not hide or disable shaders by hardware. Cutting a piece is a
  curation decision (as ridgeline was), made once with measurements, not a
  runtime behaviour.
* It does not add a per-shader hardware allowlist. A hardcoded model table
  cannot cover unreleased silicon, which is exactly why classification is a
  starting point and measurement is the authority.
* It does not change composition per tier.

## Open questions for review

1. Apple Silicon starting tier is a guess until something is measured on real
   hardware. MEDUSA is Intel+AMD; we have no M-series in the fleet.
2. Should `extra` be reachable at all on an iGPU that measures fast enough, or
   is it reserved for discrete? Current draft: reachable, because measurement
   is the authority.
3. Windows and macOS have no `wp_viewporter`. The same render-scale effect is
   a blit from the offscreen target; confirm there is no quality delta worth
   caring about versus compositor upscale.
4. `iQuality` adoption: which shaders actually earn it? Needs the per-shader
   cost sweep, which is not yet run.
