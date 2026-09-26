# neonspacewar — design

**Date:** 2026-09-25
**Status:** operator-directed, consolidated. Supersedes the layered addenda
that accumulated during dispatch; this is the single coherent spec the
phase-2 agent works from.

---

# neonspacewar — self-playing vector space battle

Repo `~/Projects/ncz-screensavers` on this host. `git fetch origin && git
rebase origin/master` FIRST and work from whatever landed, including files
phase 1 created after this was written.

**Never use /tmp** -- use `~/build-tmp/`. Git identity: Jason Perlow
<jperlow@gmail.com>. Push origin, gitlab-ncz, argonas. Other agents are
active in this repo: rebase immediately before EVERY push, check `git
branch --show-current` before committing, never force-push. Every commit
ends:
```
Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01PtHg952vKo7Y6ceAonNRXU
```

Companion docs in the repo, read them:
`docs/superpowers/specs/2026-09-25-neonspacewar-design.md`,
`2026-09-25-genxvectorcade-design.md` (shared stroke model, phosphor
decay, quality ladder), `2026-09-25-platform-abstraction-rule.md`.

---

## BUILD ORDER — read this before planning

This is a large spec. A previous dispatch on a large spec spent 89
minutes and 115 tool calls exploring and made zero edits. Do not do that.

**Make your first file edit within 15 tool calls.** If you cannot, stop
and report what is blocking you.

Work in this order, committing and pushing each step as it completes:

1. **Rename** (below) — its own commit, first, nothing else in it.
2. **Stroke renderer** — the single highest-value piece. Get gorgeous
   glowing vector strokes with per-channel phosphor persistence working
   against the existing phase-1 entities before adding anything new.
3. **One race pair fighting** — two generated races, two distinct
   weapons, one special ability each, in the existing arena.
4. **One hazard** — the gravity well, since the arena already needs it.
5. **Then** expand: more races, more hazards, engagement lifecycle.

Do not build the full roster and hazard set before anything is on screen.

---

## 1. FIRST COMMIT: rename to `neonspacewar`

Phase 1 shipped as `neonasteroids`, a placeholder from when this was a
rock shooter. It is now a space battle and the name is wrong.

`git mv src/gles3_neonasteroids.c src/gles3_neonspacewar.c`, rename any
`vendor/neonasteroids/` directory and shaders, update the meson target,
installed binary name, installed shader path and `install_data` rules.
Then `grep -rn neonasteroids .` across the whole tree and fix every hit —
source, meson, docs, `PORTED.md`, validation lists, curation docs, diag
strings, capture filenames referenced in prose. Update phase 1's roundup
doc prose; do not rewrite history or force-push.

---

## 2. What this is

A **self-playing multi-faction space battle**, in the lineage of the 1962
PDP-1 two-ship duel and the vector space-combat cabinets that followed.
No player, no score, no HUD, no text of any kind, ever. The AI drives
every side and is tuned for watchability.

Keep from phase 1: the vector rendering, trail and bloom optics, the
autonomous pilot AI, RNG seeding and `[diag]` logging, the asteroid
field.

---

## 3. THE LOOK — this is a vector piece, and the vector work is the point

**Vector line art is the language.** Gratuitous, luxuriant linework is
the goal — period hardware had a hard budget in line segments per frame;
we do not. Spend what it could only dream of, and make the spending
visible.

- **Dense structured hulls** — not outline silhouettes. Internal ribs,
  spars, lattices, engineering detail. A ship rewards looking closely.
- **Stroke quality is the craft.** Sharp bright core, soft exponential
  halo (`exp(-q*q)`, avoids a blur pass), properly antialiased at every
  scale, width and intensity varying with depth, velocity and damage.
  A great stroke renderer is most of this piece's quality.
- **Transparency and overlap as aesthetic.** Wireframes are see-through;
  overlapping geometry gives interference and moiré. Compose for it —
  ships should look best passing through each other's structure.
- **Long luxurious persistence**, per-channel phosphor decay so tails
  shift hue as they fade (blue lingers).
- **Everything is lines** — hulls, projectiles, hazard structure, debris,
  gravity-well field lines, wormhole rims, an arena lattice. Even nebulae
  read as accumulated stroke density where you can manage it.
- **Depth through line behaviour**, not shading. Near: bright, thick,
  saturated. Far: thin, dim. No shaded solids.
- **Explosions are structural disassembly** — a hull comes apart into its
  actual segments, which tumble, fade and get pulled by gravity. Do not
  substitute a particle puff; the disassembly is the money shot.
- **Peak density should be near-overwhelming**, then breathe back down.

**Raster-era accents, sparingly, on arena furniture only:** pinball-style
chasing lamp arrays and bumper geometry rimming a hazard, occasional
filled shapes where the effect demands light rather than surface. These
must never compete with the linework.

**No scanlines.** Vector tubes have none and we are not simulating a
specific display.

**Colour:** hard saturated primaries on black as the base register — pure
reds, cyans, magentas, yellows against void — with aggressive palette
cycling and hue drift moving away from it over an engagement. Maximal
colour, disciplined form. Restraint in colour is the failure mode.

### Synthesis, not pastiche — the hardest requirement here

**No single source may be identifiable.** If a viewer can point at the
screen and name a specific game, that is a defect. The target is:

> Someone who spent their childhood in arcades in the early 1980s looks
> at this and **smiles before they can say why.** It feels like a place
> they have been. They cannot name it, because it never existed.

Recognition is failure; recollection is the goal. Aim at the room — the
hum and glow of a dark hall full of cabinets, attract-mode reverie, the
feeling of watching rather than playing — not at any one cabinet's
screen.

So: blend techniques *within a single frame*, never sequence them as
set-pieces. Break every borrowed technique deliberately — take a receding
tube perspective but let it drift or invert; take a dense swarm but give
it behaviour no period hardware could run. A technique used exactly as
its source used it reads as a quote; pushed past its original limits it
reads as a memory. Avoid any signature gesture reproduced whole; if an
element only works because it is recognisable, cut it.

Let the generated content dominate. The period vocabulary is the medium,
not the subject. If the style is the most interesting thing on screen,
the balance is wrong.

**Trademark rule, absolute:** no game title, franchise, species,
character, developer, publisher or manufacturer name anywhere in the
artifact — code, comments, docs, diag strings, asset or capture
filenames, commit messages. No near-miss spellings. This is an artistic
requirement as much as a legal one: a namable reference breaks the spell.

---

## 4. The races

Generate a **pool of 8-12 races per launch**, field 3-5 in the
engagement. Persist nothing between runs — a new universe each launch.

Generate along **independent axes** and let combinations produce the
weirdness:

- **Substrate** (drives silhouette): crystalline/mineral, gaseous,
  insectile/chitinous, plant or fungal, aquatic, uplifted animal,
  synthetic/machine, hive or colony organism, energy being, something
  incomprehensible.
- **Social posture** (drives flight behaviour and targeting): zealot,
  merchant, coward, predator, parasite, jester, hedonist, ascetic,
  enslaved, enslaver, isolationist, dying remnant, expansionist swarm.
- **Technological idiom** (drives hull and weapon grammar): grown organic
  hulls, welded scrap, elegant minimal geometry, baroque ornament, brute
  mass, crystalline lattice, swarm of identical small units, one enormous
  slow thing.
- **Combat role**: glass cannon, tank, swarm, sniper, denier,
  area-control, hit-and-run.

Draw one per axis, reject incoherent combinations, name from an invented
phonology.

**Each race gets a primary weapon AND a distinct special ability.** The
special is what gives it character — a different *verb*, not a damage
modifier, describable without numbers. Invent your own; examples of the
kind: teleport to a random point; split into autonomous sub-craft; deploy
a persistent turret or minefield; brief invulnerability but cannot fire;
drag a tethered mass to swing into enemies; reverse gravity locally; fire
backwards so it fights while fleeing; consume debris to repair; emit a
cone that reflects incoming fire.

Weapons must be visually distinct at a glance: hard-cored bolts, homing
swarms that arc and jostle, persistent sweeping beams that carve trails,
spreads, delayed area bursts, chain lightning jumping between targets,
slow heavy plasma with knockback.

**Deliberately unbalanced in interesting ways.** Glass cannons, swarms,
tanks, cowards. Watchability comes from mismatched matchups, not
fairness. At least one race per engagement should be **structurally
ridiculous** — absurdly large, absurdly fragile, fighting backwards, made
of something that should not fly. Comedy is part of this.

**Personality in behaviour** — a coward keeps range and flees at low
health, a zealot charges, a hoarder farms debris. Behaviour is
characterisation.

Silhouettes must be distinguishable **as line art at small scale** —
harder than distinguishing sprites. A capture at 25% scale should still
separate the races.

The director should prefer matchups whose axes contrast over mirror
matches.

---

## 5. The arena

Keep the asteroid field — terrain the battle is fought through. Ships
dodge rocks, rocks shatter under fire, debris becomes new hazards.

Add **1-3 astrophysical hazards** per engagement, randomly chosen, each a
real tactical element with real optics:

- **Black hole** — lensing, accretion glow, an event horizon that
  destroys what crosses it. Borrow technique from the existing
  `blackhole` shader work in this repo rather than reinventing it.
  Slingshot-able.
- **Neutron star** — small, blinding, extreme local gravity, with a
  rotating pulsar beam that sweeps the field and damages what it crosses.
- **Quasar** — relativistic jets as a traversal hazard; its light washes
  and tints the whole field.
- **Nebula / ion cloud** — occludes, scatters weapon light, degrades
  homing.
- **Magnetar or plasma storm** — periodic field-wide discharge arcing
  between ships and rocks.
- **Wormhole pair** — entities crossing one emerge from the other.

**A gravity well at or near field centre** in the original duel's spirit:
curves trajectories, pulls in wreckage and slow projectiles, gives the
field a focal point. Gravity from every massive body composes onto ships,
projectiles and debris alike.

**Hazards must be tactically real.** The AI knows about them, uses them
(slingshots, luring enemies into a pulsar sweep, hiding in a nebula) and
does not die to them stupidly.

Consider staging some engagements down a **receding tube/well
perspective** rather than flat — it pairs naturally with the gravity well
and is a strong source of period character.

**Hyperspace jumps** as escape/re-entry, with a real visual event at both
ends.

Randomize per launch: race count, numbers per side, alliances (two
factions may gang up on a third), arena parameters, hazard selection and
placement. A launch's arena should feel like a specific place.

**Engagements resolve and restart.** On a win, run a short aftermath —
drifting wreckage, cooling debris, the victor reforming — then seed a
fresh engagement with new races. Never dead-end into an empty field.

Pacing: sustained readable action, varied tempo, near-misses, the field
thinning then re-escalating. Avoid both stalemate and instant wipeout.
Phase 1's pacing work applies directly.

---

## 6. Platform abstraction

Per the committed rule: pure C + GLSL ES 3.00 in hack code. No platform
headers, no POSIX-only calls, no direct sensor or network access. Time,
seed and asset paths go through the platform interface (`ncz_now()`,
`ncz_seed()`, `ncz_asset_path()`, ...). Create it if it does not exist
yet.

Shaders install to `/usr/share/ncz-screensavers/shaders/` via meson
`install_data` and must resolve from that absolute path; cwd-relative is
a dev fallback only. A cwd-relative-only path was a real ship-blocker
that killed the flagship blackhole hack while it still passed the gate.

---

## 7. Safety — non-negotiable

Full-field colour flashes synchronized to events are part of this style
and are exactly the hazard. **No sustained full-field luminance flashing
in the ~3-30 Hz band.**

**Average frame brightness is not a sufficient test.** Local regions can
flash while cancelling globally, and saturated-red transitions need
separate attention. Validate captured output **spatially as well as
temporally**, against **worst-case peak-density combat**, not a quiet
frame. Report the bound you enforced and how you measured it.

Nothing readable on screen — no text, numbers, hostnames, labels. A
radar/overview motif is permitted only as abstract blips, or omit it.

---

## 8. Performance

Line count is the primary scaling axis and the main cost. Build the
quality ladder around it explicitly: hull detail tiers, trail length,
debris segment budget, field-line density, projectile counts.

Target **under 33 ms on Intel UHD** with simplified hulls; strong
hardware gets the full lattice. Scale back rather than ship a stutter and
say what you traded.

Measure on Intel UHD, NVIDIA RTX 2060 and AMD Navi14:
- **PEGASUS** `sshpass -p pegasus ssh ... pegasus@192.168.207.85` —
  defaults to its **Intel iGPU** (your Intel test). For NVIDIA export
  `__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/10_nvidia.json`,
  `__GLX_VENDOR_LIBRARY_NAME=nvidia`, `__NV_PRIME_RENDER_OFFLOAD=1` and
  **confirm the `RENDERER=` line** — a full 141-target sweep was once
  wasted by silently running on the wrong GPU.
- **MEDUSA** (AMD Navi14): `sshpass -p medusa ssh -o
  PubkeyAuthentication=no -o IdentityAgent=none -o IdentitiesOnly=yes -o
  PreferredAuthentications=password -o StrictHostKeyChecking=no
  medusa@192.168.207.84`, repo `~/ncz-screensavers`.

Report **rendered segment counts per quality tier** alongside frame
times. "It looked fine" is not a measurement.

---

## 9. Evidence required

1. Captures across a full engagement arc — opening, peak, resolution,
   aftermath, next engagement seeded.
2. **Roster set**: each race in one frame, identifiable by silhouette and
   palette, with its four axis values stated alongside, so it is obvious
   no two collapsed onto the same design. Plus a capture at 25% scale
   showing they remain distinguishable.
3. **Each special ability** actually firing. If one cannot be recognised
   in a still, say so and describe what a viewer sees in motion.
4. **Each weapon type** in its own frame, proving visual distinctness.
5. **Each hazard** present and actually affecting the fight — a
   trajectory bent by gravity, a ship lost to a pulsar sweep, fire
   scattered in a nebula.
6. Two launches side by side proving roster, weapons, hazards and setup
   differ.
7. Frame times + segment counts on all three GPUs.
8. Runs from `/` resolving the installed shader path.
9. Non-black frame coverage percentage.
10. **The pastiche check**: look at a spread of your captures and ask
    honestly whether you can name a specific game from any single frame.
    Report what it caused you to change. A report claiming the check
    passed with zero changes will be treated as not having been done.

Do not trust the pixel-diff validation gate — it passed `mapscroller` on
both vendors while that hack rendered nothing at all. Look at the images.

---

## 10. The feeling this piece targets

Family doctrine:
`docs/superpowers/specs/2026-09-25-screensaver-family-doctrine.md`.
Each piece in this catalogue commits to **one emotional register**.

**This piece: nostalgia and spectacle.** An arcade hall the viewer
remembers but never visited. The pleasure of watching a fight you are not
playing. Smiling before you know why.

The sibling `genxvectorcade` is deliberately unrestricted — every
technique, mixed freely, aimed at wonder and hallucination. **This piece
is the opposite discipline**: a strict vector idiom, and its beauty comes
from depth within that constraint rather than from range. Do not import
that piece's anything-goes licence here; the two are meant to be
unmistakably different members of one family.

Before shipping, state in one sentence what this makes a viewer feel,
without naming a technique. If that sentence could equally describe a
sibling piece, it has not differentiated itself yet.
## Presentation modes — supersedes the shot-weight table in section 11

Operator: *"So spacewar has 2d tactical mode, 3d cinematic mode, and 3d
pov bridge mode."* Correct. Build exactly these three.

**One simulation underneath, three ways of looking at it.** The fight,
the AI, the physics and the hazards are identical in every mode; only the
camera and the presentation change. Nothing about the battle should
depend on which mode is active, and switching modes mid-engagement must
never disturb the simulation.

### Mode 1 — 2D tactical

**Orthographic top-down.** This is not merely a far-away camera; it is a
genuinely different look and it is the classic one. Flat plane, no
perspective convergence, the whole arena legible at once, ships as clean
vector silhouettes.

This is the **home mode** and should hold the majority of runtime. It is
where the battle is most readable and where the "watching a fight you are
not playing" feeling lives. When in doubt, be here.

### Mode 2 — 3D cinematic

**Perspective camera moving through the arena.** Slow orbits, dollies,
close passes, dramatic low angles through the gravity well. Parallax and
depth do the work. Shots are chosen and cut **on events** — a kill, a
hyperspace entry, a pulsar sweep, an engagement resolving — never on a
fixed timer.

Legibility is the constraint: if a shot makes it impossible to tell what
is happening, it is the wrong shot however pretty. Cut back toward wide
or to tactical when the action peaks.

### Mode 3 — 3D POV bridge

**From inside a ship, looking out.** The strongest nostalgia trigger in
the piece and therefore the most dangerous — it is the most recognisable
image from one specific cabinet, and section 3's synthesis rule makes a
faithful reproduction a defect.

So take it and break it:
- **No reticle, no crosshair, no HUD, no text or numbers.** Required by
  the lock-screen rule, and it is also what stops the shot being a quote.
- **Every race's bridge is different**, derived from its substrate axis —
  insectile sees compound faceted panels, crystalline sees refracted
  splits, a hive sees many small tiles, gaseous sees a soft diffused
  smear, machine sees a hard geometric overlay. This is worldbuilding
  where the source had one fixed viewpoint, and it is better than the
  original.
- **Rare and short.** Held too long it becomes a game the viewer cannot
  play, which is frustrating rather than hypnotic.
- Enter and leave with a real transition, not a hard cut.

### Rough time budget

Tactical ~55%, cinematic ~35%, POV bridge ~10%. Treat as a starting point
to tune against real captures, not a rule.

### Transitions matter as much as the modes

The switch between modes is a visible, designed event, not a cut. A
tactical-to-cinematic move should feel like the flat plane tilting into
depth — the same geometry, rotating into perspective, with the vector
lines carrying through. That continuity is what sells one world seen
three ways rather than three separate screensavers.

### Architecture

**The camera is 3D; the simulation stays on a 2D plane** (or a thin slab
with modest Z). This is the load-bearing decision — it buys perspective,
parallax and cinematic framing while leaving the AI, collision, gravity
and pathing in 2D where they already work. Full 3D combat would multiply
the AI and physics cost for little visible gain. In tactical mode the
same camera is simply orthographic and overhead.

Keep mode selection in one small director module with an explicit state
machine, so the cadence can be retuned without touching rendering.

### Evidence

Captures of all three modes, at least two different races' bridges in POV
showing they genuinely differ, and one capture mid-transition showing
geometry carrying through between modes. Report segment counts per mode —
a close pass and an orthographic wide shot cost very differently.

## Test host correction (2026-09-25)

**MEDUSA is 192.168.207.84, user `medusa`, password `medusa`.** Verified
working. The account was originally `medusamedusa` (an operator typo at
install time, NOT an installer defect — an earlier note in this file said
otherwise and is retracted) and was renamed in place with
`usermod -l medusa -d /home/medusa -m`, preserving uid 1000 and all file
ownership. Any brief or note using `medusamedusa@` is stale.

A second admin account `jasonperlow` with the fleet password and
passwordless sudo now exists on that host. It was added because `medusa`
was the only non-system user, which made any account change a lockout
risk. Root telnet recovery does NOT work there despite telnetd listening
on :23, so `jasonperlow` is the real recovery path.

`192.168.207.86` is dead and does not ping, despite the fleet table
listing MEDUSA there.
