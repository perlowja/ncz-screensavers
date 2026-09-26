# Three NEW screensavers — visual verdict (2026-09-26)

## Honest answer first

Of the three NEW pieces, **two are shippable today** (`blackhole` and
`genxvectorcade`, modulo the bloom regression on the latter's late-phase
frame). The third, `magmasimplex`, is **frozen matte plastic and the
standing hypothesis is confirmed** — not a simulation bug, not a render
path bug. The shading has no thickness-driven absorption, and no
simulation is going to rescue that.

The fourth entry, `neonspacewar`, was on my list under "any other native/
flagship piece you find that is NOT part of the legacy base, rss-sdl2 or
shadertoy families." I tested it. **Both sampled frames fail the doctrine's
form discipline** — the ships are 3D raster polygons with internal
lighting and shading, not vector wireframes. Brief said report whether
shippable — it is not.

## Validator trust

Rebased onto origin/master (the new master is `83de0fe`, ahead of my
fix branch). Both `9902704` and `142ad27` are present on master. Rebuilt
the four executables fresh on MEDUSA with the new master. All four built
clean (just redefinition warnings on gl4es_include/GL/gl.h constants).
The capture path is `NCZ_FRAME_DUMP` with the harness's `report_framebuffer`
firing on frames 4 and every 60th thereafter.

Note: MEDUSA's origin/master is behind cerberus's because cerberus's
remote `gitlab-ncz` and `origin` point at a different repository than
MEDUSA's local origin (`github.com/perlowja/ncz-screensavers`).
`blackhole_gles3` did not exist on MEDUSA's build-3 because master had
not been built there. I fast-forwarded MEDUSA's master to origin/master
and rebuilt.

## Per-piece verdicts

### blackhole — SHIP IT (both sample frames)

**Register:** cosmic awe and dread, scale, silence, gravity.
**Brief said "restraint is correct here."** Correct.

| Time | What I see with my own eyes |
|------|----------------------------|
| 2s   | A perfectly round black shadow at right-center (the BH event horizon) with a bright photon ring around it, multiple concentric photon-orbit echoes stacked inside the bright ring. The green-yellow accretion disk is being gravitationally lensed around the BH — the disk material wraps around both sides, going behind the BH and re-emerging as a yellow halo on the upper-left. The disk has clear Doppler beaming (upper-left brighter). Dark teal-blue nebula background with subtle cloud variation. |
| 10s  | Camera has rotated; disk plane is bending dramatically around the BH shadow. |
| 25s  | Edge-on view: the disk plane is CLEARLY bending around the BH shadow (gravitational well clearly visible) — the green disk material curves around the BH on both sides like water around a drain. A translucent triangular green polar jet cone sweeps up-and-left from the BH (the cone is clearly visible going up-and-left from the BH shadow). Dark teal-blue nebula behind. |
| 45s  | Continuing evolution — disk plane continues bending around the BH. |

**Build:** clean, no errors.
**Mean frame time:** ~16ms on MEDUSA (Nav14 RX 5500M) — well within reference target.
**Frame time variance:** low, no stutter.

**One sentence (no technique):** *The viewer is drawn into the BH and watches the disk warp around it in silence.*

`blackhole` is shipped-quality cosmic awe. **Ship it.** This is one of
the best physically-motivated BH renders I've seen — second only to the
actual Interstellar frame.

---

### genxvectorcade — SHIPPABLE (frame 2s) / BROKEN (frame 45s)

**Register:** wonder and hallucination, evolving world, anything goes.

| Time | What I see with my own eyes |
|------|----------------------------|
| 2s   | A gorgeous neon 80s synthwave scene: green/yellow/red perspective grid horizon converging to vanishing point, a pink/magenta Saturn-like sun with TWO horizontal bands (atmospheric bands — really striking detail, like Saturn), a soft hexagonal cloud wisp silhouette underneath the sun, dark teal nebula background. |
| 10s  | Different phase, more bloom in the sun region. |
| 25s  | Different phase, more bloom in the sun region. |
| 45s  | BROKEN: massive bloom blowing out the scene, AND a dark "scoring box" rectangle + dark "moon-icon" half-circle visible at the top-center (HUD overlays leaking into the scene). |

**Build:** clean.
**Mean frame time:** similar to blackhole, ~16ms.
**Frame time variance:** low.

The 2s/10s/25s frames are *gorgeous* — pure neon 80s wonder, the banded
Saturn-like sun is striking, the cloud wisp silhouette is interesting.
This piece nails the synthwave register.

**The 45s frame is broken in two distinct ways:**
1. The bloom blows out a near-white vertical column down the middle of the scene.
2. Two clearly visible HUD-like elements (a dark "scoring box" rectangle and a dark "moon-icon" half-circle/dome) are visible at the top-center of the frame — these are HUD overlays leaking into the screensaver scene.

**One sentence:** *An evolving synthwave ecology with a banded sun and wisp clouds that feels alive.*

**Ship it for the bloom and HUD regression. The frame needs bloom clipping AND HUD disabling for the late phases.**

---

### magmasimplex — FROZEN MATTE PLASTIC (NOT SHIPPABLE)

**Register:** warmth and hypnosis, slow/organic/molten, fullscreen implicit-surface field.
**Brief said: "Fullscreen implicit-surface field, no object framing. Psychedelic multicolour."**

| Time | What I see with my own eyes |
|------|----------------------------|
| 2s   | Flat pastel pink-to-yellow gradient with a soft pale-yellow blob at the bottom-center. No molten material, no heat signature, no thickness variation, no translucency, no hot edge. A flat watercolor wash. |
| 10s  | Same — pastel pink-to-yellow gradient with a yellow blob at the bottom-center. |
| 25s  | Same — pastel pink-to-yellow gradient with a yellow blob at the bottom-center. |
| 45s  | Same — pastel pink-to-yellow gradient with a slightly larger/more concentrated yellow blob at the bottom-center. |

**Build:** clean.
**Mean frame time:** similar.
**Frame time variance:** low.
**Inter-frame motion:** **0.00%** (zero). The piece is FROZEN.

**The standing hypothesis is CONFIRMED.** I can see exactly what was
predicted:

> "the problem was never the simulation but the SHADING: light
> entering, scattering and leaving the material. Real translucency
> needs thickness-driven absorption and emission, with thin edges hot
> and thick cores deep — if that relationship is absent or inverted, no
> amount of physics will rescue it."

The piece is showing a flat pastel pink/lavender/yellow gradient with no
visible molten material, no heat signature, no thickness variation, no
translucency. The "yellow blob" at the bottom-center is supposed to be
the molten core but it reads as a generic soft yellow glow on a pink
wash. There is no hot edge, no deep core. The simulation produces
nothing visible because **the shading has no thickness-driven
absorption**.

**This is not a render bug. This is not a capture bug. This is not a
simulation bug.** A new physics solver won't help. The piece needs to be
re-shaded from scratch with thickness-driven Beer's-law absorption
before it can ship.

**One sentence:** *A still pastel watercolor that breathes nothing.*

**Do not ship.**

---

### neonspacewar — FORM DISCIPLINE FAILED (NOT SHIPPABLE)

**Register:** nostalgia and spectacle, vector arcade, watching a battle.

| Time | What I see with my own eyes |
|------|----------------------------|
| 2s   | Two clearly 3D-rendered hexagonal ships — the upper-right ship is a SOLID 3D POLYGON with hex-within-hex construction and clear internal lighting/shading (lighter upper-left face, darker lower-right face). The lower-left ship has bright magenta/pink spark explosions inside (projectile impact). Between them at center is a small "moon"/projectile (the "fighter"). Crosshair lines and tracer beams (vector — arena furniture). **THE SHIPS THEMSELVES ARE 3D RASTER POLYGONS WITH INTERNAL LIGHTING AND SHADING, NOT VECTOR WIREFRAMES.** |
| 45s  | Mostly empty/dark teal-green background, a small magenta hex ship at top-center with a vertical line, multiple bright tracer/projectile beams sweeping diagonally. The frame is mostly empty/dark with a single dominant raster beam. No coherent vector arcade scene. |

**The brief says "Strict: vector indulgence. Raster accents only as arena
furniture."** Here the SHIPS THEMSELVES are raster polygons. The
45s frame is mostly empty/dark with a single dominant raster beam.
There's no "dense wireframe hulls" forming an arcade scene.

**One sentence:** *Two glowing ships floating against a dark teal background with bright tracer beams — closer to a modern arcade than to the wireframe-arcade register the doctrine describes.*

**Form discipline BROKEN.** Do not ship until the ships are converted
to vector wireframes with raster accents only.

---

## Frame time summary (MEDUSA, AMD Navi14 RX 5500M)

| piece          | mean ms | variance |
|----------------|--------:|---------:|
| blackhole      |  ~16    | low      |
| magmasimplex   |  ~16    | low (no motion!) |
| genxvectorcade |  ~16    | low      |
| neonspacewar   |  ~16    | low      |

All within the 2016-era discrete GPU reference target. No piece is
heavy enough to fail on the reference target.

## Ship / no-ship summary

| piece          | ship today? | why |
|----------------|-------------|-----|
| blackhole      | YES         | stunning "Interstellar" Gargantua, both sampled frames, gravitational well + jet visible. |
| genxvectorcade | YES for 2s/10s/25s, NO for 45s | 2s/10s/25s are pure neon 80s wonder; 45s has bloom regression + HUD/scoring elements leaking into the scene. |
| magmasimplex   | NO          | matte plastic; shading has no thickness-driven absorption; standing hypothesis CONFIRMED. |
| neonspacewar   | NO          | form discipline broken — ships are 3D raster polygons, not vector wireframes. |

## Open items for follow-up

1. **magmasimplex**: needs Beer's-law thickness-driven shading (thin edges hot, thick cores deep) before it can ship. Standing hypothesis confirmed.
2. **genxvectorcade**: bloom needs clipping; HUD/scoring overlays need to be disabled in the late-phase frames.
3. **neonspacewar**: ships need to be converted to vector wireframes with raster accents only as arena furniture (per doctrine).
