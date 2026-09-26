# Neon space battle (working name: neonasteroids) — design

**Date:** 2026-09-25
**Status:** operator-directed. Phase 1 (self-playing vector rock shooter)
built and validating; this document specifies the phase-2 redirect into a
self-playing multi-faction space battle, plus the astrophysical hazard
arena and the race-design/visual-language direction that followed.

Sibling documents: `2026-09-25-genxvectorcade-design.md` (shared optics,
stroke model, quality ladder), `2026-09-25-platform-abstraction-rule.md`.

---

# neonasteroids phase 2 — turn it into a self-playing space battle

Repo `~/Projects/ncz-screensavers` on this host. `git pull origin master`.
Phase 1 (the self-playing rock shooter you just built) is the foundation —
keep the vector rendering, the trail/bloom optics, the autonomous pilot AI
and the RNG-seeding work. This phase changes what the pilot is doing.

**Never use /tmp** -- use `~/build-tmp/`. Git identity: Jason Perlow
<jperlow@gmail.com>. Push origin, gitlab-ncz, argonas. Other agents are
active in this repo: `git fetch && git rebase origin/master` immediately
before EVERY push, check `git branch --show-current` before committing,
never force-push. Every commit ends:
```
Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01PtHg952vKo7Y6ceAonNRXU
```

## The change in direction

Operator: *"Neon asteroids should be a space battle type environment,
randomized engagements between alien races. Give them different weapons.
I'm envisioning the old 'Star Trek' arcade game. Or 'Spacewar'. But more
colorful and modern shader effects. Minterized."*

So: **not a lone ship shooting rocks. A randomized multi-party space
battle that plays itself**, in the lineage of the 1962 PDP-1 two-ship
duel and the vector-graphics space combat cabinets that followed it —
rendered with modern shader work and psychedelic colour.

**Naming/trademark rule, strict:** no third-party trademarks, brand,
product-line, franchise or manufacturer names anywhere — not in code,
comments, docs, diagnostic strings, asset names, or commit messages. The
references above describe the *feel* we want; they must not appear in the
artifact. Invent original faction names. "Minterized" describes an
aesthetic — extreme saturation, layered feedback, gleeful excess,
animal-and-geometry iconography, colour cycling that borders on too much
— implement the aesthetic, never the name.

## What to build

### Factions — randomized per launch
3 to 5 factions per engagement, generated from a seed rather than
hardcoded. Each gets its own:
- **Hull language.** Distinct vector silhouette grammar — angular wedges,
  radial/mandala symmetry, organic curves, segmented crystalline forms,
  insectile. Procedurally generated from faction parameters so no two
  runs field identical designs.
- **Palette.** A faction reads instantly by colour before shape resolves.
- **Weapon.** This is the operator's explicit ask and the main source of
  visual variety. Give each a genuinely different projectile with
  different tactical behaviour AND different optics:
  - straight-line bolts with hard cores and long halos
  - homing swarms that arc and jostle
  - beams that persist and sweep, carving trails
  - spreads/shotguns
  - mines and delayed area bursts
  - chain/arc lightning jumping between nearby targets
  - slow heavy plasma with strong bloom and knockback
  Randomize which faction has which, and randomize weapon parameters
  within a type. Weapon variety must be visible at a glance.
- **Flight behaviour.** Aggressive brawler, cautious sniper, swarming
  skirmisher, ponderous capital ship. Behaviour should be readable from
  motion alone.

### The engagement
- A **gravity well** at or near field centre, in the original duel's
  spirit: it curves trajectories, pulls in wreckage and slow projectiles,
  and gives the field a focal point. Ships use it — slingshots, orbits.
  Make it visually gorgeous (lensing, accretion glow) since we already
  have blackhole shader work in this repo to borrow technique from.
- **Hyperspace jumps** as an escape/re-entry mechanic, with a real visual
  event on both ends.
- Randomized engagement setup per launch: faction count, numbers per
  side, alliances (two factions may gang up on a third), arena
  parameters. The operator's standing direction for this whole family is
  *different every time it runs*.
- **Engagements resolve and restart.** When a side wins, run a short
  aftermath (drifting wreckage, cooling debris, the victor's formation
  reforming), then seed a fresh engagement with new factions. It should
  never dead-end into an empty field.

### Pacing — a screensaver, not a game
No player, no score, no HUD, no text of any kind. The AI drives both
sides and should be tuned for *watchability*: sustained readable action,
varied tempo, near-misses, moments where the field thins and then
re-escalates. Your phase-1 pacing work (preferring kills that keep the
field interesting) applies directly — extend the same thinking to
avoiding both stalemate and instant wipeout.

### Optics — where "Minterized" lives
- Heavy additive layering, feedback trails, chromatic separation.
- Per-channel phosphor decay so colour lingers differently by channel.
- Explosions as real events: flash, shockwave ring, radial debris, a
  lingering glow pool.
- Palette cycling and hue drift over the course of an engagement.
- Push saturation hard. Restraint is the failure mode for this piece.

## Safety — non-negotiable
No sustained full-field luminance flashing in the ~3-30 Hz band. This
piece is full of explosions, so this is a real constraint, not a
formality: **average frame brightness is not a sufficient test** — local
regions can flash while cancelling globally, and saturated-red
transitions need separate attention. Clamp the rate and extent of
full-field flashes, validate captured output spatially as well as
temporally under worst-case dense combat, and report how you bounded it.

## Platform abstraction
Per `docs/superpowers/specs/2026-09-25-platform-abstraction-rule.md`:
pure C + GLSL ES 3.00, no platform headers, no POSIX-only calls in hack
code. Time/seed/asset-path go through the platform interface.

## Performance
Target under 33 ms on Intel UHD. Scale projectile counts, ship counts and
post-processing by a quality level rather than shipping a stutter. Report
frame times on Intel UHD, NVIDIA RTX 2060 and AMD Navi14 (hosts and PRIME
env vars as in your phase-1 brief — confirm the `RENDERER=` line, a full
sweep was once wasted by silently running on the wrong GPU).

## Evidence required
1. Captures across a full engagement arc — opening, peak, resolution,
   aftermath, next engagement seeded.
2. Captures proving **weapon variety is visually distinct** — ideally one
   frame per weapon type.
3. Two launches side by side proving factions, weapons and setup differ.
4. Frame times on all three GPUs.
5. Runs from `/` resolving the installed shader path.
6. Non-black frame coverage percentage.

Do not trust the pixel-diff validation gate — it passed `mapscroller` on
both vendors while that hack rendered nothing at all. Look at the images.

---

# ADDENDUM — the arena is a hazardous place, not empty space

Operator: *"Still want rocks and other obstacles including black holes,
quasars, neutron stars, etc."*

Keep the asteroid field from phase 1. It is not replaced by the battle —
it is terrain the battle is fought through. Ships must dodge rocks,
rocks shatter under fire, debris becomes new hazards, and a pilot that
flies into one pays for it.

Beyond rocks, populate the arena with randomly chosen **astrophysical
hazards**, 1-3 per engagement, each a real gameplay element with real
optics, not a backdrop:

- **Black hole** — lensing, accretion disc, an event horizon that
  destroys anything crossing it. Borrow technique from our existing
  `blackhole` shader work in this repo rather than reinventing it.
  Strong gravity; slingshot-able by a skilled pilot.
- **Neutron star** — small, blindingly bright, extreme local gravity.
  Give it a rotating pulsar beam that sweeps the field and damages what
  it crosses, which doubles as a spectacular recurring visual event.
- **Quasar** — a distant, violent light source with relativistic jets.
  Its jets should be a traversal hazard and its light should visibly
  wash and tint the whole field.
- **Nebula / ion cloud** — soft volumetric region that occludes,
  scatters weapon light, and degrades homing.
- **Magnetar or plasma storm** — periodic field-wide electrical
  discharge that arcs between ships and rocks.
- **Wormhole pair** — entities crossing one emerge from the other, which
  the AI should occasionally exploit.

Requirements:
- Randomize which hazards appear, where, and their parameters. A given
  launch's arena should feel like a specific place.
- **Hazards must be tactically real.** The AI knows about them, uses
  them (slingshots, luring enemies into a pulsar sweep, hiding in a
  nebula) and avoids dying to them stupidly. Gravity from every massive
  body composes onto projectiles, debris and ships alike.
- Hazards are also the main source of visual grandeur here — a
  quasar-lit battle in front of an accreting black hole is the shot we
  want. Spend the shader budget accordingly.
- This adds real cost. Fold hazard fidelity into the quality ladder so
  Intel UHD still holds 30 fps, and say what you scaled.

Evidence: add captures showing each hazard type actually present and
actually affecting the fight — a trajectory bent by gravity, a ship lost
to a pulsar sweep, weapon fire scattered in a nebula.

---

# ADDENDUM 2 — race design philosophy, and the 1980s look

Operator: *"Space battle we can take a page from Star Control, with weird
races, but the look should be 1980s."*

Two separate instructions. Take the **design philosophy** from the
early-90s asymmetric-roster space-melee genre; take the **visual
language** from 1980s vector arcade hardware. Do not take that genre's
own 90s pixel-art look.

**Trademark rule, unchanged and strict:** no third-party franchise,
company, product or character names anywhere — code, comments, docs,
diag strings, asset names, commit messages. Invent every race name. The
reference above describes a design approach; it must not appear in the
artifact.

## What to take from that genre

Its real innovation was that **every ship is a different design problem,
not a stat variation.** Apply that:

- **Each race gets a primary weapon AND a distinct special ability**, and
  the special is what gives it character. Not a damage modifier — a
  different verb. Examples of the *kind* of thing (invent your own, do
  not copy a roster):
  - teleports to a random point in the arena
  - splits into several smaller autonomous craft
  - deploys a stationary turret or minefield that persists
  - becomes briefly invulnerable but cannot fire
  - drags a tethered mass it can swing into enemies
  - reverses or nullifies gravity locally
  - fires backwards, so it fights while fleeing
  - consumes nearby debris to repair itself
  - emits a cone that reflects incoming fire
- **Deliberately unbalanced, in interesting ways.** A race may be
  enormous, slow and devastating; another tiny, fragile and absurdly
  fast. Glass cannons, swarms, tanks, cowards. Asymmetry is the point —
  watchability comes from mismatched matchups, not fairness.
- **Weird, not generic.** The silhouettes should be strange: asymmetric,
  lopsided, organic, insectile, crystalline, or plainly ridiculous. A
  viewer should be able to tell races apart in one frame at a glance.
- **Personality in behaviour.** Each race's AI should fly in character —
  a coward keeps range and flees at low health; a zealot charges; a
  hoarder farms debris. Behaviour is characterisation.
- Fold this into the existing randomized-engagement structure: pick 3-5
  races per launch from a procedurally generated pool, so matchups and
  even the roster differ every run.

## The look: 1980s vector arcade, not 1990s sprites

This is the constraint that keeps it coherent with the rest of the
GenXVectorCade family.

- **Everything is line art.** Stroked vector geometry, no textured
  sprites, no pixel art, no raster bitmaps for ships or hazards.
- **Glowing phosphor strokes** — sharp bright core with a soft halo, as
  in the stroke model already specified in
  `docs/superpowers/specs/2026-09-25-genxvectorcade-design.md`. Reuse it.
- **Per-channel phosphor decay and persistence trails**, the way a real
  vector tube smears. Blue lingers.
- Colour comes from **saturated stroke colour and overexposed cores**,
  not from filled shading. Vector hardware of that era could not fill;
  lean into that and let it define the style.
- **Filled polygons are allowed only where the effect demands it** —
  nebula volumes, accretion glow, shockwaves — and should read as light,
  never as a textured surface.
- Subtle CRT character: slight bloom, faint geometric distortion,
  scanline-free (vector tubes have no scanlines — do not add them). Keep
  it as medium, not subject.

The 1980s reference is about *technique and restraint in form*, not about
colour restraint. The palette stays maximal and psychedelic per the
earlier direction — think what those machines would have done with
unlimited colour guns. Excess in colour, discipline in form.

## Evidence addition

Add a capture set showing **the roster**: each race in one frame,
identifiable by silhouette and palette, plus a capture of each special
ability actually firing. If a race's special cannot be recognised in a
still, say so and describe what a viewer sees in motion.
