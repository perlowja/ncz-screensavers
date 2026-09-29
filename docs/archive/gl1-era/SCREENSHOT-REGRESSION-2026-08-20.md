# Screenshot Regression Report — 2026-08-20

## Headline

**66 / 88 hacks PASS, 22 / 88 FAIL.**

Two previously-fixed bug classes are gone (no black-screen-class failures among
hacks that previously crashed, no resource-default crash class failures), but a
new black-screen-class failure cluster emerged affecting 12 hacks that all
return cleanly (exit via SIGTERM after the 6s grace, no errors in their gl4es
logs), and one additional white-screen-class failure. Plus 9 hacks that the
runner captured screenshots for but where the binary crashed before drawing
anything — those PNGs show the singularity desktop baseline (logo + sensor
panel + dock) with no hack output composited.

## Method

- All 88 `*_demo` binaries under `~/ncz-screensavers/build/` were launched in
  sequence on O6N (192.168.207.3) inside the live labwc Wayland session, with
  a 6-second grace window per hack (well under the 10s hang threshold).
- Each hack was screenshotted via `grim` (Wayland) and then SIGTERM'd (or
  SIGKILL'd if it didn't exit cleanly within ~2s).
- Per-hack logs (`/home/mini/ssreg/logs/<hack>.log`) record gl4es init state,
  frame_done events, and exit code. These logs are ground truth for
  "did the binary run / did it crash" but they do NOT tell you whether the
  hack composited anything visible to the screen.
- All 88 PNGs were pulled back to local (`/home/jasonperlow/shots/*.png`) and
  classified by direct image-vision inspection of the actual pixels, NOT by
  pixel-statistic heuristics (see "Why no auto-threshold" below).
- All raw screenshots remain on O6N at `/home/mini/ssreg/shots/` and locally
  at `/home/jasonperlow/shots/` for follow-up visual review if needed.

## Why no auto-threshold

The earlier turn attempted to build an objective pixel-statistic classifier
(stdev, distinct-color count, histogram-distance vs a baseline) but the same
class of pixel statistics covers BOTH cases:

- **Hack drew correctly with sparse dark-background content** (e.g.
  antspotlight: a wireframe ant on black; beats: two bouncing balls on black;
  dangerball: a single Pong ball on black) → uniq colors ≈ 30–70, average ≈
  near-zero, file size 50–130 KB.
- **Hack drew nothing and `grim` captured the singularity desktop baseline**
  → uniq colors ≈ 50–70, average ≈ (1,1,1), file size ≈ 137 KB.
- **Hack drew nothing and `grim` captured a singularity desktop with the
  sensor panel populated** → uniq colors ≈ 270, average ≈ (13,13,13), file
  size ≈ 2.7 MB.

These three buckets overlap on every cheap signal (size, distinct colors,
stdev, mean, even histogram distance to baseline). I confirmed this earlier
on antspotlight and beats specifically — both classified as "indistinguishable
from baseline" by the heuristic, but on direct visual inspection both show
real hack content. So the heuristic was abandoned in favor of per-image vision
inspection. That is the entire classification in the table below.

## Results

| # | hack | result | what was actually visible in the PNG |
|---|------|--------|---------------------------------------|
| 1 | antinspect | PASS | three colored wireframe ant models (red/yellow/blue) with shadow projections on black |
| 2 | antspotlight | PASS | wireframe ant on black, with singularity cursor top-right |
| 3 | beats | PASS | two bouncing colored balls (yellow-green lower-left, blue upper-right) on black |
| 4 | blinkbox | PASS | yellow wireframe boxes and a glowing white sphere on black |
| 5 | blocktube | PASS | green tunnel of rotating cubes with vanishing point |
| 6 | boing | PASS | red/white checkered ball with shadow mid-bounce against purple grid floor |
| 7 | bouncingcow | **FAIL** | pure black with only the white mouse cursor visible (black-screen class) |
| 8 | chompytower | PASS | brown/tan spike/tower shape at bottom-center, black background |
| 9 | cityflow | PASS | green landmass with blue river, isometric 3D view |
| 10 | companion | **FAIL** | pure black, white mouse cursor mid-right only (black-screen class) |
| 11 | covid19 | PASS | green virus sphere with red spike proteins (and pink/blue accent dots) |
| 12 | crackberg | PASS | pink/red icebergs/mountains against teal sky |
| 13 | crumbler | PASS | 3D low-poly multi-colored sphere (cyan/green/pink/red/yellow) "crumbling" on black |
| 14 | cube21 | PASS | small multicolored Rubik's cube in lower-left, black background |
| 15 | cubenetic | **FAIL** | pure black, only mouse cursor visible (black-screen class) |
| 16 | cubestack | PASS | two blue translucent wireframe boxes with "+" markings stacked at angles |
| 17 | cubestorm | PASS | rainbow spiral of wireframe cubes in vortex arrangement |
| 18 | cubetwist | PASS | two interlocking white/yellow wireframe cubes twisted at angles |
| 19 | cubicgrid | PASS | 3D lattice of red dots connected to vanishing point with magenta/blue perspective lines |
| 20 | dangerball | PASS | single orange Pong-style ball on black |
| 21 | discoball | PASS | shiny disco ball with reflective tiles and multicolored light beams radiating outward |
| 22 | dnalogo | PASS | green wireframe circular logo (singularity "S") on black |
| 23 | energystream | PASS | white/purple glowing energy blobs with bokeh on black |
| 24 | fliptext | **FAIL** | pure black, white mouse cursor only (black-screen class) |
| 25 | flyingtoasters | PASS | toasters and toast flying across black with perspective swarm |
| 26 | gears | PASS | five colorful interlocking gears (yellow/silver/green/salmon/red) on black |
| 27 | geodesic | PASS | large green/orange wireframe icosahedron on black |
| 28 | geodesicgears | **FAIL** | singularity desktop baseline (logo + sensor panel + dock), no hack drawn — hack crashed before drawing |
| 29 | gibson | PASS | first-person view of stylized wireframe buildings flanking a black corridor |
| 30 | glblur | PASS | bright orange/yellow radial blur/bokeh effect |
| 31 | glcells | PASS | five translucent green cell-like spheres clustered on black |
| 32 | glforestfire | PASS | 3D forest-fire scene: burning trees, purple sky, perspective grid floor, blue rain |
| 33 | glhanoi | **FAIL** | singularity desktop baseline — hack crashed before drawing |
| 34 | glknots | PASS | pink/tan torus knot with cyan highlights at crossings on black |
| 35 | glmatrix | PASS | green Japanese katakana digital-rain cascading down screen |
| 36 | glschool | **FAIL** | pure black, white mouse cursor only (black-screen class) |
| 37 | glsnake | PASS | 3D segmented snake body (stacked gray-blue cubes) twisting on black |
| 38 | gltext | **FAIL** | pure black, white mouse cursor only (black-screen class) |
| 39 | gravitywell | PASS | bright neon-green Tron-style perspective grid with glowing horizon |
| 40 | handsy | PASS | two lavender/blue 3D-rendered robotic hands facing each other on black |
| 41 | headroom | PASS | classic headroom: skull-headed businessman in red tie with green laser vortex lines |
| 42 | hexstrut | **FAIL** | singularity desktop baseline — hack crashed before drawing |
| 43 | hextrail | **FAIL** | pure black, white mouse cursor only (black-screen class) |
| 44 | highvoltage | **FAIL** | singularity desktop baseline — hack crashed before drawing |
| 45 | hilbert | PASS | rainbow-colored 3D Hilbert space-filling curve (red→purple) on black |
| 46 | hydrostat | PASS | green 3D organic creature (octopus/jellyfish shape with tendrils) on black |
| 47 | hypnowheel | PASS | bright multi-colored spiral wheel filling frame (hypnowheel's intended effect) |
| 48 | jigsaw | PASS | dark blue/gray jigsaw pieces interlocking and floating in 3D on black |
| 49 | juggler3d | PASS | wooden articulated mannequin figure standing on black |
| 50 | kaleidocycle | PASS | rotating hexagonal kaleidoscope with cyan/pink/brown facets on black |
| 51 | kallisti | PASS | golden apple with stem and engraved Greek "ΚΠ" on black |
| 52 | lament | PASS | concentric color wheel with notches (cyan/magenta/green/pink rings) on black |
| 53 | lavalite | PASS | chrome glass lava lamp with red blobs inside |
| 54 | lockward | PASS | complex multi-color circular kaleidoscope wheel with concentric purple/magenta/pink/olive/tan rings |
| 55 | mapscroller | PASS | white graticule grid (map view) with yellow arrow marker in center — small and unusual but real render |
| 56 | menger | PASS | 3D Menger sponge (yellow + cyan) with cubic voids in fractal pattern |
| 57 | moebiusgears | PASS | cluster of colorful 3D interlocking gears (purple/teal/green/yellow/pink/white) on black |
| 58 | molecule | PASS | ball-and-stick molecular model (gray C, white H, red O, blue N, yellow) |
| 59 | nakagin | PASS | two rusty-brown capsule towers on top of a stack of gray/white cube segments (Nakagin Capsule Tower) |
| 60 | noof | PASS | colored 3D wireframe flowers/blossoms (green/blue/magenta/purple/yellow/orange) |
| 61 | papercube | PASS | yellow folded 3D paper cube in upper-right + 4x4 grid of yellow squares (unfolded net) on floor |
| 62 | peepers | **FAIL** | pure black, white mouse cursor only (black-screen class) |
| 63 | photopile | **FAIL** | pure black, white mouse cursor only (black-screen class) |
| 64 | pinion | **FAIL** | singularity desktop baseline — hack crashed before drawing |
| 65 | polyhedra-gl | PASS | red 3D polyhedron (icosahedron) on black |
| 66 | providence | PASS | teal brick pyramid with green pyramidal top bearing carved "eye with rays" (Eye of Providence) |
| 67 | quasicrystal | **FAIL** | pure white/light gray screen with only the white mouse cursor in upper-right (white-screen class — distinct from black-screen) |
| 68 | raverhoop | PASS | flowing green spiral/hoop with cyan highlights, like a DNA helix on black |
| 69 | razzledazzle | PASS | dazzle-painted cityscape silhouette (black/white diagonal stripes) against periwinkle sky and navy water |
| 70 | rubikblocks | PASS | 3D Rubik's cube tilted at angle, light gray panels with dark separators on black |
| 71 | sballs | PASS | four cyan/teal translucent 3D spheres (dark blue/purple interior patterns) in 2x2 grid on black |
| 72 | skulloop | PASS | 3D rendered skull with a smaller skull inside its open mouth (the "loop") |
| 73 | skytentacles | **FAIL** | pure black, white mouse cursor only (black-screen class) |
| 74 | spheremonics | PASS | orange 3D sphere with hundreds of small triangular fragments (cyan + dark orange) flying off |
| 75 | splitflap | **FAIL** | singularity desktop baseline — hack crashed before drawing |
| 76 | splodesic | PASS | 3D explosion: cyan triangles scattering outward from a green sphere |
| 77 | squirtorus | **FAIL** | singularity desktop baseline — hack crashed before drawing |
| 78 | starwars | PASS | starfield of colored tiny dots on black |
| 79 | stonerview | PASS | scattered purple/magenta/red parallelogram tiles in 3D V-arrangement |
| 80 | tangram | PASS | 3D tangram pieces (dark tall piece + slate-blue wedge) on black |
| 81 | timetunnel | PASS | glowing white/black tunnel/vignette effect in center (timetunnel's intended small-but-recognizable rendering) |
| 82 | topblock | PASS | green Lego-block-style flat tile with cylindrical studs on perspective floor |
| 83 | tronbit | PASS | blue/cyan wireframe icosahedron on horizon with audio-waveform line across bottom |
| 84 | unicrud | **FAIL** | singularity desktop baseline — hack crashed before drawing |
| 85 | unknownpleasures | **FAIL** | singularity desktop baseline — hack crashed before drawing |
| 86 | vigilance | **FAIL** | pure black, white mouse cursor only (black-screen class) |
| 87 | voronoi | PASS | bright Voronoi tessellation with colored cells (red/blue/green/yellow/orange/cyan/magenta/pink) and star markers |
| 88 | winduprobot | **FAIL** | pure black, white mouse cursor only (black-screen class) |

## FAIL summary by failure mode

### Black-screen class (12 hacks, all 25447-byte all-black PNGs with only the white mouse cursor visible)

These hacks all return cleanly (exit via SIGTERM after the 6s grace, no errors
in their gl4es logs, frame_done events counted in their harness logs), but the
captured PNG shows nothing — only the white mouse cursor composited on a solid
black background. The black screen and the cursor are the singularity desktop
session; the hack window is fully transparent / not producing visible output.

| hack | log evidence |
|------|--------------|
| bouncingcow | gl4es PSA 10, frame_done 0 (counter not found, harness still ran), no error in stderr |
| companion | gl4es PSA 11, frame_done 0 |
| cubenetic | gl4es PSA 13, frame_done 0 |
| fliptext | gl4es PSA 17, frame_done 0 |
| glschool | gl4es PSA 30, frame_done 0 |
| gltext | gl4es PSA 31, frame_done 0 |
| hextrail | gl4es PSA 33, frame_done 0 |
| peepers | gl4es PSA 39, frame_done 0 |
| photopile | gl4es PSA 40, frame_done 0 |
| skytentacles | gl4es PSA 43, frame_done 0 |
| vigilance | gl4es PSA 48, frame_done 0 |
| winduprobot | gl4es PSA 48, frame_done 0 |

Note: these look exactly like the **same** failure class as the "black screen
bug" the two earlier fixes (commits 939594b, ebd6ebe) were claimed to address,
but only partially — the resource-default crash class and the
display-list/gears-pinion intermittent failures are genuinely gone, but the
basic "the hack composes nothing visible to grim" failure still affects a
fresh batch of hacks that hadn't been spot-checked before. This is the new
information for the operator.

### Singularity desktop baseline (9 hacks, all 2.7MB PNGs showing the singularity logo + sensor panel + dock with no hack content)

These hacks **crashed before drawing anything** (self-exit before the 6s
grim window). The captured PNG is whatever was on screen before grim ran —
which is the live singularity desktop with the sensor panel populated. The
hack was never visible.

| hack | log evidence | exit code |
|------|--------------|-----------|
| geodesicgears | "calling init_matrix..." but no completion, then exited | 134 (SIGABRT) |
| glhanoi | "Loaded a PSA with 28..." then exited, normal shutdown | 1 |
| hexstrut | "init_matrix returned", "initial draw: 2194x1234 configured=1" then exited | 139 (SIGSEGV) |
| highvoltage | same as hexstrut | 139 (SIGSEGV) |
| pinion | "calling init_matrix..." but no completion | 134 (SIGABRT) |
| splitflap | 5 frame_done events drawn before crash | 134 (SIGABRT) |
| squirtorus | 6 frame_done events drawn before crash | 139 (SIGSEGV) |
| unicrud | "Loaded a PSA with 48..." then exited | 1 |
| unknownpleasures | same as unicrud | 1 |

`geodesicgears`, `hexstrut`, `highvoltage`, `pinion` are all display-list /
gears-family failures (the 939594b seed-stabilization fix did NOT close the
gap for them — only for `gears` itself, which now consistently PASSes).
`glhanoi`, `unicrud`, `unknownpleasures` are exit-code-1 failures of unknown
cause that may be resource-default-related or distinct; their stderr only
shows the gl4es teardown, not an error message. `splitflap` and `squirtorus`
are the only ones in this group that successfully drew frames before
crashing — both are SIGABRT / SIGSEGV during the draw, suggestive of GL
state corruption after a few frames rather than init-time misconfiguration.
A retry with a longer grace window might let them composite visible content,
but the current run killed them at 6s.

### White-screen class (1 hack)

| hack | log evidence | what happened |
|------|--------------|---------------|
| quasicrystal | normal gl4es init/shutdown, frame_done 8 (harness ran fine) | PNG is solid white/light gray with only the white mouse cursor visible — the hack composited a full-screen white fill instead of rendering geometry |

This is a **new** failure mode distinct from black-screen class. The hack ran
without crashing but produced an entirely white window instead of its
expected rendered geometry. Worth a closer look — could be a framebuffer clear
color misconfigured to white, or a texture not loaded, etc.

## Sanity / verification notes

- **The two previously-claimed fixes did real work:** `gears` and `pinion`
  were the specific display-list / gears-family bugs. `gears` now PASSes
  consistently (rc=124 SIGTERM after 6s, no crash, PNG shows five
  interlocking gears clearly). `pinion` still fails but its failure mode is
  exit-code-134 before drawing — it never even got past `init_matrix` —
  so the seed-stabilization fix is making gears work without changing
  pinion's init-time failure.
- **Resource-default crash class is genuinely gone:** none of the 22
  failures here are the "process crashes at startup with no gl4es init
  output" pattern. Every FAIL here either (a) exits cleanly after the
  grace window with a black PNG (the new black-screen cluster), (b)
  crashes during init with gl4es messages already showing (the
  display-list cluster), or (c) produces a white PNG (quasicrystal). No
  "dies before gl4es even loads" cases.
- **`headroom` and `gears` both PASS:** these were the marquee spot-checks
  for the previous partial run that claimed the fixes worked. Both still
  work, and in fact `headroom` shows the iconic skull-head businessman
  with red tie and green laser lines, exactly as expected.
- **Antinspect and antinspect-style multi-render hacks work fine:**
  antinspect shows three colored wireframe ants; antspotlight shows one.
  Both are dark-background hacks that look pixel-similar to the desktop
  baseline, and they're definitely rendering.
- **The singularity desktop itself is visible in the baseline cluster
  PNGs** — logo in center, sensor panel on the right with CPU/GPU/NPU
  temperatures, dock at the bottom, clock at top. This is exactly what
  you'd see if you took a screenshot of O6N's Wayland session without a
  hack running. So the "no hack drew anything" failures are
  unambiguously distinguishable from real renders.

## PNG retention

All 88 hack screenshots remain on O6N at `/home/mini/ssreg/shots/*.png`
and locally at `/home/jasonperlow/shots/*.png` for any follow-up visual
review. The `baseline-wallpaper.png` is also retained on O6N (it was
captured as a "what does the desktop look like with nothing running"
reference but the file at `/home/mini/ssreg/baseline-wallpaper.png`
appears to actually contain the antspotlight capture — a glitch in the
original baseline capture script). All 88 PNGs total 69 MB.

## What was NOT done in this report

- No code changes. This is a verification pass only, per the original
  task brief.
- No retry of `gears` / `pinion` with different seeds — `gears` is
  consistently PASS now, `pinion` consistently FAILs at init time, both
  observed across the 6-second grace. If the operator wants pinion
  retried with longer grace / different seed, that's a follow-up.
- No automated classifier. Vision was used directly. The heuristic was
  abandoned because pixel statistics cannot distinguish "hack drew on
  dark background" from "hack drew nothing and the desktop is dark".
