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

---

# Astra's second opinion (consulted 2026-09-25)

Complements GRAEAE rather than contradicting it. Its framing:
**"an arcade whose light has acquired a history of its own."**

## Technique additions

**Sharp core plus halo without a blur pass** — cheaper than a bloom stage:
```glsl
float aa = max(fwidth(d), 1e-5);
float core = 1.0 - smoothstep(width - aa, width + aa, d);
float q = d / haloRadius;
float halo = exp(-q * q);
vec3 emission = core * coreColour + haloStrength * halo * haloColour;
```
Note this gives *strokes* a halo; it does not bloom arbitrary neighbouring
imagery, which still needs extra sampling.

**Max-based trail envelope**, so persistence cannot accumulate unbounded:
```glsl
vec3 decay = exp(-vec3(dt) / tauRGB);
vec3 trail = max(currentEmission, previousTrail * decay);
```
Keep long expressive trails *separate* from the short afterglow that
suggests phosphor — two different systems, not one.

**Use integer ticks, not a forever-growing float time uniform.** Float
precision degrades over a multi-day run. Persist ecological state,
genomes, RNG state, simulation tick and director history in a versioned
format.

## SAFETY CORRECTION — important

Our earlier framing (watch average frame brightness) is **insufficient**.
Astra:

> *"Local regions can flash while their contributions cancel globally;
> saturated-red transitions warrant separate attention."*

Validation must be **spatial as well as temporal**, run against captured
output including worst-case dense states and moving fine patterns, with
red transitions checked separately. Reference:
<https://www.w3.org/WAI/WCAG21/Understanding/three-flashes-or-below-threshold>

## Additions to the avoid list

- **A brightness/entropy fitness score** — rewards visual noise,
  saturation, or one dominant strategy.
- **Unrestricted mutation** — destroys recognisable inheritance and
  yields mostly nonviable organisms.
- **Full-image feedback as the sole state** — smears and saturates, and
  entangles presentation with biology.
- **Permanent spectacle** — removes the contrast that makes spectacle
  mean anything.
- **Strict one-pass purity** — can *increase* total cost while obstructing
  persistent causality. Multi-pass where it genuinely pays.

## Build order: one vertical slice first

**One evolutionary habitat, two display materials, one depth reveal, one
complete dramatic phrase** — before expanding the repertoire. Then require
evidence in four areas:

- **Evolution** — births inherit traits; mutations alter behaviour; those
  changes affect reproductive success.
- **Ecology** — multi-hour runs across many seeds recording diversity,
  extinction, stagnation and recovery.
- **Presentation** — an unfamiliar viewer can recognise a lineage and
  describe something that happened to it.
- **Performance** — sustained baseline-hardware runs meeting the frame
  budget, including dense states and thermal settling.

---

# Environmental input: the machine's own sensors

Operator direction: drive the world's emotion from real environmental
data — and, better, **from the local system's own sensors**.

This is the literal reading of "Tron for real": the world inside the
machine, driven by the actual state of the machine. It beats any network
source on every axis — no privacy exposure, no offline failure mode, no
latency, and the ecosystem becomes *this machine's* inner life rather
than a generic feed.

| Sensor | Source | Ecological meaning |
|---|---|---|
| CPU / GPU temperature | `/sys/class/hwmon`, `/sys/class/thermal` | climate; warmth drives metabolism and mutation rate |
| Fan RPM | `hwmon` | wind — flow-field strength and direction |
| CPU load, per-core | `/proc/stat` | population pressure, colony growth |
| Memory pressure | `/proc/meminfo` | density, crowding |
| Disk I/O | `/proc/diskstats` | seismic events, terrain disturbance |
| Network throughput | `/proc/net/dev` | migration, arriving species |
| Battery / charging | `/sys/class/power_supply` | available energy; on battery the world dims and conserves |
| Ambient light | `/sys/bus/iio/devices` | the room's lighting, where present |
| Uptime | `/proc/uptime` | the ecosystem's age |

**Why thermal is the best one:** it serves the hardware-longevity thesis
directly. An older machine running hot with fans up produces a more
turbulent, dramatic world. The hardware's struggle becomes the weather —
old hardware reads as *characterful* rather than degraded. It also gives
a viewer discoverable causality: start a compile, and the world blooms.

**Clock-derived inputs need no sensors at all** — local time, sun
elevation and moon phase are pure computation, giving a genuine circadian
rhythm and a slow lunar cycle for free.

**Optional network layer, if ever wanted: space weather, not local
weather.** NOAA SWPC publishes free, keyless, *global* feeds (Kp index,
solar wind speed/density, X-ray flux). Being global, it carries no
location-privacy problem, and it maps thematically — real electromagnetic
weather driving the arcade's electromagnetic weather. A genuine
geomagnetic storm becomes a rare bloom that actually corresponds to
something happening, which is exactly the "rare events must feel earned"
requirement.

## Engineering requirements for any sensor input

- **Every sensor is optional.** `hwmon` paths and labels vary widely;
  many desktops expose no fan or battery; VMs expose almost nothing.
  Each input needs graceful absence and a synthetic fallback.
- **Zero network I/O in the hack.** Any network-derived value arrives via
  a cache file written by a separate service, exactly as designed for the
  stock visualisations. Stale or absent cache falls back to synthetic
  drift. A screensaver must never look broken because wifi is down.
- **Sensor reads must not be on the render path.** Poll on a slow timer;
  the shader consumes smoothed uniforms.
- **Smooth everything.** Raw load and temperature are spiky; the world
  should feel weather-like, responding over tens of seconds, not
  flickering per frame.
- **Nothing readable.** Values drive visual qualities only. No numbers,
  gauges or readouts — a screensaver runs on a locked screen.
- **Platform abstraction applies** (see the platform-abstraction rule):
  sensor access sits behind the platform interface, not in hack code.
  Windows and macOS have entirely different sources.

---

# Environmental inputs, FINAL: local sensors AND space weather

Operator direction: **use both.** They divide naturally by timescale,
which maps onto the five-timescale requirement rather than duplicating it.

| Layer | Cadence | Drives |
|---|---|---|
| **Local system sensors** | seconds to minutes | the world's moment-to-moment texture — climate, wind, activity, crowding |
| **Space weather** | hours to days | cosmic seasons, long eras, and genuinely rare spectacular events |

Local is required and always available. Space weather is an enrichment
layer that must degrade to synthetic drift without it.

## Verified NOAA SWPC endpoints (checked live 2026-09-26)

Free, keyless, **global** — no location privacy problem, because it is the
same data for every machine on Earth.

| Endpoint | Status | Sample |
|---|---|---|
| `https://services.swpc.noaa.gov/json/planetary_k_index_1m.json` | **200**, ~28 KB, 1-minute cadence | `{"time_tag":"2026-09-25T19:36:00","kp_index":2,"estimated_kp":2.00,"kp":"2Z"}` |
| `https://services.swpc.noaa.gov/products/summary/solar-wind-speed.json` | **200**, 59 B | `{"proton_speed": 491, "time_tag": "2026-09-26T01:29:00Z"}` |
| `https://services.swpc.noaa.gov/json/goes/primary/xray-flares-latest.json` | **200**, 421 B | `{"satellite": 18, "current_class": "B4.3", ...}` |
| `https://services.swpc.noaa.gov/products/noaa-scales.json` | **200**, ~1 KB | R/S/G scales with 24h probabilities |

**Paths that do NOT exist** — do not re-derive these, they 404:
`/products/solar-wind/plasma-2-hour.json`,
`/products/solar-wind/plasma-1-day.json`,
`/products/solar-wind/mag-1-day.json`.

## Mapping

| Input | Range seen | Ecological meaning |
|---|---|---|
| **Kp index** | 0-9, quiet is 0-3 | mutation pressure and event frequency. A real geomagnetic storm (Kp 6+, a handful of times a year) becomes a rare ecosystem bloom |
| **Solar wind speed** | ~300-800 km/s | energy input — growth rate, luminosity of the field |
| **X-ray flare class** | A/B/C/M/X | punctuation. An M or X flare is a genuine, earned spectacular event |
| **NOAA R/S/G scales** | 0-5 | slow era-setting; sustained storm conditions shift the whole palette and behaviour for hours |

This solves a problem GRAEAE raised directly: rare events must feel
**earned rather than canned**. An event driven by an actual solar flare is
earned in the strongest possible sense — something really happened, 150
million kilometres away, and the ecosystem responded.

## Architecture — unchanged discipline

- A small service polls SWPC on a slow timer (Kp updates every minute but
  meaningfully changes over hours; **15-30 minute polling is ample** — do
  not hammer a free public service).
- It writes a tiny cache file. **The hack performs zero network I/O.**
- Stale or absent cache, no network, offline laptop -> synthetic slow
  drift. The screensaver must never look broken because wifi is down.
- Cache values are smoothed; space weather should feel like season, not
  like a data feed.
- Nothing readable. No numbers, no gauges, no readouts — this runs on a
  locked screen.
- Network access sits behind the platform abstraction, not in hack code.

## Why this combination is right

Local sensors make the world **this machine's** — its heat, its load, its
fans, discoverable by anyone who starts a compile and watches the
ecosystem bloom.

Space weather makes it **everyone's** — every machine running this shares
the same sky, so a geomagnetic storm is felt simultaneously by every
instance on Earth. Two people running it in different countries see
related weather on the same night.

That pairing is the whole idea in miniature: intensely local, quietly
shared, and neither one requiring anyone to give up anything private.
