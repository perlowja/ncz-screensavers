# GenXVectorCade — a love letter to Generation X

**Date:** 2026-09-25. Operator direction, verbatim:
*"I want this to be a love letter to Gen X."*
*"It should be a constantly evolving screensaver, a never ending work of
art that changes automatically."*

This is the creative spine for the flagship screensaver work. It governs
GenXVectorCade and informs MagmaSimplex, the black hole and the vector
shooter.

## The love letter

Gen X is the only cohort that watched computer graphics get invented in
real time — from vector arcade cabinets to the demoscene to MTV to the
first screensavers. The visual vocabulary to draw on:

- **Vector arcade, 1979-83.** Glowing line geometry on black, phosphor
  bloom, forced perspective, wireframe everything. Monitors that actually
  drew lines rather than pixels.
- **8/16-bit home computers.** C64 and Amiga demoscene: copper bars,
  rasters, plasma, rotozoomers, sine scrollers, Kefrens bars, twisters.
- **CRT physicality.** Phosphor persistence and decay, scanlines, slight
  barrel curvature, bloom halation, chromatic fringing at the edges.
  Screens used to *glow* and *smear*; that is a feature to reproduce, not
  an artifact to remove.
- **VHS.** Tracking wobble, colour bleed, dropout. Sparingly.
- **MTV / neon / synthwave.** Grid horizons, chrome suns, magenta and
  cyan, airbrush gradients.
- **Rave and acid house.** Strobing colour fields, kaleidoscopes,
  smiley-era saturation, light-synth visuals.
- **Cyberpunk cinema.** Tron's light grids, Blade Runner's haze and
  scanning beams, Akira's reds.
- **The screensavers themselves.** Starfields, pipes, flying toasters,
  After Dark's whole deadpan surrealism. We are making a screensaver about
  screensavers.

The tone is affectionate, not ironic. Not a museum piece and not a
parody — it should feel like the thing you remembered being, which is
always better than the thing that actually was.

## The architecture: one never-ending artwork

**The important instruction is that it never ends and never repeats.**
That is a structural requirement, not a styling note, and it changes how
this is built.

### 1. No loops

Nothing on a fixed cycle. Drive every parameter from incommensurable
(irrational-ratio) periods so combinations never exactly recur. A viewer
watching for an hour should never see the same frame twice; someone
glancing daily for a year should keep finding new states.

### 2. Movements, not scenes

Continuous morphing between states — tunnel, grid horizon, starfield
warp, kaleidoscope, lattice. Transitions are authored as first-class
material, not cuts. Geometry *becomes* the next thing.

### 3. Generative sequencing

The order and duration of movements is generated, not scripted, and
reseeded per launch. Some passages are rare — a state that appears once
an hour is a gift to whoever is watching when it happens.

### 4. It should evolve across sessions

Persist a small amount of state so today does not start where yesterday
did. The artwork has a life longer than one screen-blank.

### 5. Eventually: one continuous stream, not a picker

The long-term shape is that the user does not choose *a* screensaver.
The system flows continuously through the good ones, morphing between
them, as a single evolving work. That is what "Showcase mode" in the
chooser design should grow into — a stream, not a random pick.

## Constraints that still hold

- Real-time on Intel UHD. Measure, do not assume.
- **No sustained rapid full-field luminance flashing (~3-30 Hz).** Real
  photosensitive-seizure risk. The rave/strobe references above are an
  aesthetic to evoke through colour, density and motion — never by
  actually strobing.
- No third-party names, trademarks, game titles, companies or logos in
  code, comments, docs or commits. The eras and genres are ours to draw
  on; specific properties are not.
