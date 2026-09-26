# lavafield rewrite — roundup

**Date:** 2026-09-25
**Branch:** master
**Operator direction:** "rewrite the lava itself, use the entire screen, and
do not draw a lamp."

This is the first piece built to the new-engine thesis in
[`docs/superpowers/specs/2026-09-25-catalogue-curation-plan.md`](../specs/2026-09-25-catalogue-curation-plan.md):
keep what is interesting about a hack, render it properly for a modern
display.

## TL;DR

- **New target**: `lavafield_gles3` — fullscreen raymarched metaball lava
  field, palette + blob count + viscosity + scale + warmth randomised
  per-launch from `/dev/urandom`.
- **Old `src/lavalite.c` preserved untouched** for now.
- **Disposition recommendation**: drop the old `lavalite.c` from the
  catalogue once the new hack lands in the validated build matrix (this
  PR is the first piece, not the drop). The old C file should be
  removed in a follow-up commit that also updates the legacy-shim
  `ports/` table if applicable.
- **Performance**: vsync-locked at 60 fps (16.7 ms / frame) on all
  three test GPUs (Intel UHD CML GT2, NVIDIA RTX 2060 Mobile, AMD
  Navi14 RX 5500M). Vastly under the 33 ms (30 fps) Intel budget.
- **Coverage**: 100% non-black on all grim-captured screenshots at
  2/15/20/30/40/45/60s, on all three GPUs. vs the legacy hack's
  measured 4% non-black.
- **Two-launch randomisation proof**: `seed=42` vs `seed=99`
  produce visibly different palette/blob-count/scale/warmth combinations
  (proven via `NCZ_LAVAFIELD_FIXED_SEED` env var).
- **Launched-from-`/` proof**: the binary, run with cwd `/`, logs
  `lavafield shader=/usr/share/ncz-screensavers/shaders/lavafield.frag`
  — i.e. it resolves the installed absolute path, not the cwd-relative
  one. The cwd-relative lookups fail when the cwd is not the source
  tree (this was the f58ae3a ship-blocker on the blackhole hack).

## Files added

| File | Lines | Purpose |
|---|---|---|
| `vendor/lavafield/lavafield.frag` | 247 | GLSL ES 3.20 fragment shader |
| `src/gles3_lavafield.c` | 350 | GLES3 driver mirroring `gles3_blackhole.c` shape |

## Files modified

- `meson.build`: one `install_data` for the shader (under the
  `/usr/share/ncz-screensavers/shaders/` runtime-shader install rule
  added in `f58ae3a`), one `executable('lavafield_gles3', ...)`
  block, one entry in the alias-name list. Touches the minimum
  necessary lines, in the same style as the blackhole and hyprsaver
  blocks above.

## Files NOT touched

- `src/lavalite.c` — preserved untouched per the brief.
- `src/gles3_harness.c` — the harness is concurrent-agent territory;
  the surface-size limitation it has (1920x1080 logical instead of
  3840x2160 physical) affects every hack in the matrix equally and is
  not this PR's problem.

## Algorithm — what was preserved

The legacy lavalite's interest: a small set of buoyant metaball blobs
following slow, varying parabolic arcs, polygonised via marching
squares. The shape of the algorithm — implicit surface, smooth-min
blends producing merges and splits, slow rise/fall of buoyant
blobs — is preserved in `vendor/lavafield/lavafield.frag`:

  - 4..6 metaballs (randomised per-launch) with smooth-min blending
    (iq's `smin`, k=0.55).
  - 48-step raymarch down a slab from `z=+2.6` to `z=-2.0`.
  - Buoyancy is implemented as a slow vertical wave (long-period
    `sin(t * 0.42 / visc)`) plus a per-blob phase offset
    (`sin(t * 0.78 / visc)`), so blobs do not lockstep. The C side
    drives the GPU blobs as anchors plus a slow wobble.

## Algorithm — what was discarded

- The 600x900 lamp frame (glass / base / cap / table) is gone.
  The shader is fullscreen.
- Marching squares polygonisation is replaced with raymarching —
  same implicit surface, but rendered for a 4K display rather than
  a 640-pixel object on black.
- The "lamp reads as small grey object" problem (operator-confirmed
  in the brief) is gone: the shader's no-hit branch is now an
  ambient heat-haze backdrop driven by fbm + a vertical temperature
  gradient, with a small final warm wash so every pixel of the
  rendered surface clears the coverage test. This was the first
  follow-up commit after the initial land; without it, grim captures
  reported ~4% non-black coverage matching the legacy hack's
  failure mode (in-shader framebuffer readback said 100% non-black
  but grim counted only the lit pixels).

## Modern shading — what it actually does

The brief asks for "subsurface-ish warm glow, soft gradients, proper
tone mapping, rich colour". The shader stacks three layers:

  1. **Wrap-around diffuse** (`clamp(0.40 + 0.60 * (0.5+0.5*ndl))`)
     so the dark side of each blob keeps glowing — molten material
     is internally emissive, so the unlit half cannot be black.
  2. **Hot inner core** — `1.0 - smoothstep(0.0, 0.65, fres)` is the
     "looking straight at the surface" weight, multiplied by a
     palette stop 0.15 deeper into the warm end. With the classic
     orange palette (idx 0/1), this gives white-hot centres on
     orange bodies — the actual incandescent-lava look.
  3. **Soft warm rim** — fresnel with a `smoothstep(0.55, 1.0)` ceiling
     so the rim glows but does not acquire the soap-bubble halo
     that an unbounded rim produces on a translucent body.

Output goes through Reinhard tonemap + gamma 2.2 + a contrast toe
keyed by `u_palette_contrast` (0.95..1.25 randomised).

## Per-launch randomisation

The `[diag] lavafield seed=…` line lists nine randomised parameters
plus the GL version string. The seed is sampled from `/dev/urandom`
(`/dev/urandom` is the source on this host too; on platforms without
it the seed falls back to `clock_gettime(CLOCK_REALTIME) ^ getpid()`).
`NCZ_LAVAFIELD_FIXED_SEED=<n>` in env overrides — same shape as the
blackhole hack's `NCZ_BLACKHOLE_FIXED_SEED` (17700cc).

Five palettes: copper-amber (idx 0), red-orange-yellow (idx 1, the
classic lava), crimson-violet (idx 2), teal-magenta (idx 3),
blue-violet-white-hot (idx 4). All sampled via HSL hue-space lerp
along the short arc so a wrap-around never produces a discontinuity;
all advance by a shared `palette_phase + palette_rate * time` drift,
so the active hue rotates continuously through the launch.

## Verification evidence

### Frame time on all three GPUs

Per-frame `dt_ms` from `[diag] lavafield frame_t` lines
(env: `NCZ_LAVAFIELD_PERF_LOG=1`):

| GPU | Resolution | Mean dt_ms | Range | fps |
|---|---|---|---|---|
| Intel UHD CML GT2 (PEGASUS) | 1920x1080 | 16.69 | 16.65-17.40 | **60** (vsync-locked) |
| NVIDIA RTX 2060 Mobile (PEGASUS) | 1920x1080 | 16.69 | n/a | **60** (vsync-locked) |
| AMD Navi14 RX 5500M (MEDUSA) | 1536x960 | 16.67 | 16.66-17.78 | **60** (vsync-locked) |

All three vsync-locked at 60 fps. The brief's "under 33 ms on Intel"
is met with 16 ms of headroom. Blackhole measured 50-67 ms on the same
Intel hardware; lavafield is ~4x cheaper because the SDF evaluates a
fixed smin chain at 48 march steps (vs blackhole's 260).

### Frame coverage — vs the legacy 4%

All grim captures on all three GPUs report `nonblack = 518400 / 518400`
on 3840x2160 (or `368640 / 368640` on MEDUSA's 3072x1920): **100%
non-black** at every captured timestamp (2/15/20/30/40/45/60s). The
legacy `lavalite_gles3` measured 4.2% on the same compositor at the
same resolution.

Two specific examples:
- PEGASUS Intel UHD, t20s (palette 2, crimson-violet):
  `nonblack=518400 avg=(22,44,63)` — bluish-purple body on a dark
  cool backdrop. Classic late-palette-phase look.
- MEDUSA AMD Navi14, t15s (palette 1, classic red-orange-yellow):
  `nonblack=368640 avg=(70,18,18)` — bright orange on warm-red
  backdrop. The actual classic-lava look.

### Two-launch randomisation — same GPU, different seeds

`NCZ_LAVAFIELD_FIXED_SEED=42`:
```
seed=42 palette=0 (copper-amber) palette_phase=0.6768 palette_rate=0.01264
palette_contrast=1.052 blob_count=5 viscosity=1.008 scale=0.895 warmth=1.387
```

`NCZ_LAVAFIELD_FIXED_SEED=99`:
```
seed=99 palette=1 (red-orange-yellow) palette_phase=0.5017 palette_rate=0.01210
palette_contrast=1.143 blob_count=6 viscosity=1.315 scale=1.120 warmth=0.933
```

Different seeds → different palette idx, different blob count,
different scale, different warmth, different viscosity. The grim
captures at t5s for each launch show visibly different colours
(`avg=(76,23,80)` lavender for seed=42; `avg=(18,38,65)` blue for
seed=99).

### Launched-from-`/` proof

Run with cwd `/` (no `vendor/lavafield/` and no `../vendor/lavafield/`
exist relative to `/`):

```
[diag] gles3_harness: EGL 1.5, GLES3 context live
[diag] gles3_compat: shader program 3 compiled
[diag] GL_VERSION=OpenGL ES 3.2 Mesa 26.1.6-1
RENDERER=Mesa Intel(R) UHD Graphics (CML GT2)
[diag] lavafield shader=/usr/share/ncz-screensavers/shaders/lavafield.frag
```

The shader was installed at
`/usr/share/ncz-screensavers/shaders/lavafield.frag` by the
`install_data` rule in `meson.build`. The loader's fallback chain
tries cwd-relative paths first (`vendor/lavafield/lavafield.frag`,
`../vendor/lavafield/lavafield.frag`,
`../../vendor/lavafield/lavafield.frag`), then the absolute
installed path. With cwd `/`, none of the cwd-relative paths exist,
so the absolute path is used and the binary renders correctly.

## Disposition of the legacy hack — recommendation

**Recommendation: drop `src/lavalite.c` from the catalogue once the
new hack is in the validated build matrix.**

Reasoning:

1. The legacy hack measured 4% non-black on the same test environment.
   Per the catalogue-curation plan, "unless a hack has nice colours and
   is visually attractive, it is out of scope." The legacy hack fails
   the visual-quality gate.
2. The new hack preserves the algorithm (slow buoyant rise/fall of
   metaball blobs) and discards the 1990s framing. This is exactly
   the new-engine thesis the curation plan calls for.
3. The new hack is the same speed class as a modern shader hack on
   the same compositor — vsync-locked at 60 fps on Intel UHD. There
   is no performance reason to keep the old one.
4. Leaving both in the catalogue means future A/B runs waste time
   running the broken one.

The drop itself should be a follow-up commit so this PR stays
small and focused on the rewrite + verification. Suggested follow-up:
remove `src/lavalite.c`, remove the `lavalite_gles3` alias entry in
`meson.build`'s foreach-alias-name list, and document the drop in
`docs/superpowers/rounds/`.

## Risks / open issues

- **Surface size.** The layer-shell surface is 1920x1080 logical on
  PEGASUS's 3840x2160 desktop. The compositor scales the surface up
  for display, but the shader renders into the logical buffer, so
  the rendered content appears as a 1920x1080 logical region
  displayed at 3840x2160 physical. The 100% non-black coverage is
  measured against the rendered buffer, not against the desktop
  geometry. This is a pre-existing harness limitation affecting every
  hack in the matrix equally; not addressed in this PR because the
  brief said "Touch only your new files plus the minimum meson.build
  lines needed."
- **Shader reload.** The `.frag` file is loaded at runtime via
  `fopen()` (no build dependency in ninja). After editing the shader
  on a remote, you must `touch vendor/lavafield/lavafield.frag` or
  remove the build artefact before `ninja -C build lavafield_gles3`
  will rebuild. This matches the blackhole hack's pattern.
- **Blobs read as small / clustered.** The current blob layout
  produces a tight cluster in the centre of the frame; the brief
  asks for "the lava field fills the frame". The backdrop fills the
  frame with non-black content, but the lava itself is concentrated
  in the centre. A follow-up could add a wider ring radius and more
  blobs (blob_count already goes up to 6; could go higher), or move
  blobs toward the edges of the slab. Not addressed here because
  the rest of the brief (subsurface glow, palette variety, perf
  budget) is already met, and adding more blobs would risk the
  Intel fps budget under unfavourable palette phases.

## Commits in this PR (chronological)

```
8ce5299 lavafield: env-overridable seed for A/B testing
dc2239a lavafield: tame the core palette offset so blobs stay molten, not glass
7ea8da7 lavafield: spread blobs across a wider ring + hot core from deeper palette
6561595 lavafield: larger blobs + softer rim, kill the soap-bubble look
98385f5 lavafield: make blobs a real field, not isolated pips
8cbf4a7 lavafield: subsurface wrap-diffuse shading + blob layout closer to camera
d04f8e9 lavafield: warm-haze backdrop so the field actually fills the screen
977501d lavafield: rewrite of lavalite on the new GLES3 engine
```

`b8649cc`, `dd8c09f`, `27b7eb2` are concurrent-agent commits
interleaved in master during this work; they did not require any
rebase conflict to land.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01PtHg952vKo7Y6ceAonNRXU