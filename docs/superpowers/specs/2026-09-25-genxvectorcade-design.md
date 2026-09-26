# GenXVectorCade — an evolving phosphor ecosystem

**Date:** 2026-09-25. GRAEAE consulted (winning muse openai, 94s).
Astra consultation pending; this will be updated with its view.

## The concept

> *"It does not depict a game; it depicts the dream-state of an arcade
> after the players have gone home."* — GRAEAE

Not a game, not a fake game screen, not a demo-mode of one. A **living
light ecology** whose material language is arcade-era display physics.
GRAEAE's reframe is the key to the whole thing:

> *"Not the games, but the electromagnetic weather of the arcade."*

Operator direction that shapes it: a **randomized artificial-life system,
but it's an arcade world** — *"think Tron for real"* — **different every
time it runs**, watchable for hours like art, retro and modern at once,
and a deliberate demonstration that 6-10 year old GPUs still have real
capability even though they cannot host modern AI.

## Architecture: low-res life, full-res optics

The central performance idea, and what makes this viable on Intel UHD:

- **Low-res state texture** (256x144 to 720p by quality tier) carrying the
  simulation.
- **Full-res render pass** samples and stylises it through an arcade
  optical model.
- Ping-pong FBO for state; temporal accumulation preferred over spatial
  sampling.

State layout (RGBA16F preferred, RGBA8 with encoding as fallback):
```
state.r = life density
state.g = secondary chemical / energy
state.b = genome / species
state.a = age / activity / phase
```

## The life system: reaction-diffusion with an evolving genome

GRAEAE ranked the candidates. **Lenia/continuous CA is the most
organism-like but its convolution kernels are expensive on weak hardware;
Gray-Scott reaction-diffusion with spatially-varying evolving parameters
is the better fit.** Neural CA needs a training pipeline we do not have.
Boids are awkward without compute — useful as a visual layer, not the core.

The genome lives in the B channel and drives feed/kill rates:
```glsl
float genome = c.b;
float feed = mix(0.020, 0.060, genome);
float kill = mix(0.045, 0.070, fract(genome * 3.17));
float reaction = a * b * b;
float da = 1.0 * lap.x - reaction + feed * (1.0 - a);
float db = 0.5 * lap.y + reaction - (kill + feed) * b;
```

### Avoiding heat death AND explosion

The specific failure modes are convergence and runaway. Regulate by
**activity-driven mutation pressure** — mutate harder where the field is
too still *or* too violent:
```glsl
float activity = length(next - c.rg);
float mutationPressure =
    smoothstep(0.0, 0.01, 0.02 - activity) +
    smoothstep(0.25, 0.5, activity);
genome += (hash - 0.5) * 0.0005 * mutationPressure;
```
Dead regions get recolonised by neighbours rather than reset. **Death and
rebirth must be graceful** — a collapsing region fades into phosphor noise
and is slowly re-invaded, never hard-reset.

## The optics: display physics, not a filter

**Phosphor persistence is essential**, and per-channel decay is what sells
it — blue lingers longer than red and green, as real phosphors do:
```glsl
vec3 prev = texture(u_prevFrame, uv).rgb;
vec3 phosphor = prev * vec3(0.94, 0.965, 0.985);
vec3 color = max(emissive, phosphor);
```

Layers, in order:
1. **Dark arcade room base** — near-black, subtle vignette, low-frequency
   colour contamination. The screen emits *into darkness*.
2. **Cabinet glow zones** — overlapping pools of saturated light
   (cyan/magenta/amber/red/green) drifting in parallax, with local surges.
   Not rectangular panels; no hard screen borders.
3. **Vector apparition layer** — SDF arcs, capsules, Lissajous knots,
   concentric rings, starbursts, polar grids. The *grammar* of old
   cabinets without being a shooter or a maze.
4. **Raster/CRT layer** — scanlines, phosphor mask, slight subpixel
   offset, gentle barrel distortion. **The optical medium, never the
   subject.**
5. **Halation** — cheap cross-kernel sample of the previous frame, squared
   for a soft bloom without a real blur pass.

## Five timescales — what sustains hours

| Scale | Content |
|---|---|
| 0-10 s | motion, trails, shimmer, organisms moving |
| 10-60 s | local blooms, pulses, vector sweeps, glow shifts |
| 1-10 min | species migration, colony growth, palette drift |
| 10-60 min | ecological eras: one behaviour dominates, then yields |
| session-scale | **persisted evolution — the machine develops its own visual lineage** |

Mostly continuous drift, punctuated by rare events. Good rare events: a
vector storm crossing the field; regions phase-locking into a synchronised
attract-mode pulse; a dormant colony blooming; a colour species invading
another region. Bad ones: full-screen flashes, hard cuts, obvious resets,
"fireworks mode".

The test for a 30-second glance is readable composition. The test for a
3-hour stare is **ancestry and consequence** — *"that magenta lattice has
been taking over the left side for twenty minutes."*

## Different every time, and a history

Persist the genome and state texture to disk. Each machine's instance
diverges over weeks into its own lineage. This is what makes it an
artificial-life construct rather than a random parameter jukebox — and
GRAEAE is blunt that the difference is perceptible:

> *"Randomness is not evolution. Viewers can feel when nothing has
> memory."*

## The hardware thesis, honoured properly

Requirement: show that 6-10 year old systems still have life in them.
GRAEAE's guidance is that **dense real-time simulation plus optical
rendering** is the right way to demonstrate that — **not** brute-force
raymarching, which *"proves only that the GPU is suffering."*

Quality ladder with visible scaling:

| Tier | State res | Samples | Features |
|---|---|---|---|
| Low | 256x144 | 4-8 | simple CRT mask, no bloom kernel |
| Medium | 512x288 | 8-16 | phosphor accumulation, small glow, vector layer |
| High | ~720p | richer convolution | better bloom, more vector primitives |
| Ultra | full | multi-pass bloom | larger kernels, higher precision, richer grading |

On weak machines it stays alive and stylish; on strong ones it becomes
denser, smoother, more luminous.

Old-GPU hygiene: `texelFetch` for state, avoid branches in inner loops,
fixed sample counts per tier, hash/value noise rather than fractal noise
everywhere, temporal accumulation over spatial samples, `mediump` where
banding allows.

## What to avoid (GRAEAE's list, adopted)

- **A fake arcade game.** Score, lives, enemies, levels or a player avatar
  violate the core requirement.
- **Trademark-adjacent quotation.** No recognisable mazes, paddles,
  invaders, falling blocks, ghosts.
- **Pure Shadertoy spectacle** — impressive for 30 seconds, exhausting
  for 30 minutes.
- **Random parameter jukebox.**
- **Hard looping animation**, especially obvious sin/cos camera paths.
- **Full-screen flashing** (also our hard safety limit: nothing sustained
  in the ~3-30 Hz photosensitive band).
- **Overdone CRT filter** — scanlines plus curvature plus aberration plus
  noise becomes costume.
- **Too much neon grid** — reads as generic synthwave, not arcade presence.
- **Raymarch-first design.**
- **CA left alone with no ecology** — build regulation, mutation,
  recolonisation and history.
- **Pixel art as the main motif** — *"pixel art says 'game asset'.
  Phosphor says 'arcade room'."*
