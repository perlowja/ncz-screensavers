# Classics on the shader engine

Branch `feat/classics-shader-port-2026-09-29`. The xscreensaver classics used to run through
`src/gles3_compat.c` (a GL1 immediate-mode emulation: `glBegin`, display lists, fixed-function
lighting, the client-array `glDrawArrays` wrapper). That path is CPU-bound in the driver and,
as it turned out, also wrong in places. Nine of the twelve shipped classics are now native
GLES3 programs in `src/classics_shader/cs_<hack>.c`; the other three (hypertorus, klein,
projectiveplane) were already GLSL with retained buffers upstream and are kept as they are.

Binary names, hack ids, `HACK_TABLE` symbols and packaging are unchanged (`<hack>_gles3`).
The GL1 originals stay behind `-Dlegacy-classics=true` for one release, as
`<hack>_legacy_gles3` (installed only in that configuration) for A/B comparison.

## How a port is built

* One source file per hack that exports the same `xscreensaver_function_table` symbol as the
  original, so `src/gles3_harness.c`, the option parser, render scale, the software-renderer
  guard, the stats output and the launcher are untouched.
* `cs_common.h`: option macros (`style`, `speed`, `seed`, `bloom`, `antialias`), a seeded RNG,
  a fixed-step clock (the originals advanced per frame at a nominal rate such as 33 or 50
  steps per second; a port runs the same steps at the same rate in real time, so motion is
  identical at 30, 60 or 144 fps), matrix helpers, shader helpers.
* `cs_post.h`: optional scene FBO, quarter-resolution glow, FXAA-lite, vignette, dither. Used
  only by the `enhanced` style; the `classic` style renders straight to the window.
* Shaders are compiled once in `init`. No immediate mode, no client arrays, no per-frame
  `glBufferData`; dynamic data goes through 3-deep rings of `glBufferSubData`.
* Plain `glDrawArrays` is never used: the compat layer overrides it with its GL1 wrapper and a
  draw with no client arrays silently does nothing. Full-screen triangles are
  `glDrawArraysInstanced(..., 1)`.
* Options are declared once, in the hack, and dumped by `<hack>_gles3 --dump-schema` into
  `assets/screensaver-chooser/options/<hack>.tsv` (same columns as `blackhole.tsv`).
  Environment prefix `NCZ_<HACK>_`, config group `<hack>`.

## Styles and GPU classes

`--style=classic|enhanced|auto`.

* `classic`: faithful port. The look of the xscreensaver original, cheap: static or instanced
  geometry, per-fragment lighting only where the original lit, no extra passes.
* `enhanced`: the same hack with anti-aliasing, softer lighting and depth cue, glow, smoother
  palettes and transitions. Every effect is individually gated (`--bloom=0` removes the glow
  passes, `--antialias=false` the edge passes).
* `auto` (default): `enhanced` on the medium class and better, `classic` on the weak class,
  using `ncz_gpu_tier` (Intel iGPUs and older mobile parts are LOW; `NCZ_GPU_TIER=low|medium|high`
  overrides). The decision is logged as `[diag] classics style=auto -> ...`.

## Performance (before and after)

Method: vsync-paced runs of 12 seconds under the exclusive host lock, no readback in the loop, numbers are the harness's own swap-to-swap statistics after the first 5 seconds (`[stats] steady`). 60 fps means the display refresh was met with the listed p95 frame interval; fps below 60 is 1000 / median. `legacy` is the GL1 build (`-Dlegacy-classics=true`), `classic` and `enhanced` are the ports. Raw rows: `docs/classics-shots/perf.tsv`.

Caveat: other test agents share these machines. A run that overlapped another fullscreen window can stall (window occluded, no frame callbacks); such runs are rejected or repeated, and one combination (noof enhanced on PEGASUS) could not be completed cleanly and is marked n/a.

### O6N (Radxa Orion O6N, Mali-G720, 1080p render cap)

| hack | legacy GL1 | classic | enhanced |
|---|---|---|---|
| voronoi | 35 fps / p95 29.8 ms | 60 / 16.7 | 60 / 16.7 |
| gravitywell | 60 / 19.4 | 60 / 16.7 | 60 / 16.7 |
| hexstrut | 60 / 16.7 | 60 / 16.7 | 60 / 16.7 |
| cubestorm | 2 fps / p95 767.1 ms | 60 / 16.7 | 60 / 16.7 |
| crackberg | 14 fps / p95 83.5 ms | 60 / 16.7 | 60 / 16.7 |
| cityflow | 60 / 16.7 | 60 / 16.7 | 60 / 16.7 |
| noof | 60 / 16.8 | 60 / 16.7 | 60 / 16.7 |
| geodesic | 12 fps / p95 88.7 ms | 60 / 16.7 | 60 / 16.8 |
| gibson | 9 fps / p95 185.1 ms | 60 / 16.7 | 60 / 16.7 |

Not ported (upstream GLSL): hypertorus 60 / 16.7, klein 60 / 16.7, projectiveplane 60 / 16.7.

### MS-R1 (Sky1, Mali-G720, 4K panel, 1080p render cap)

| hack | legacy GL1 | classic | enhanced |
|---|---|---|---|
| voronoi | 56 fps / p95 28.9 ms | 60 / 16.7 | 60 / 16.7 |
| gravitywell | 60 / 21.6 | 60 / 16.7 | 60 / 16.7 |
| hexstrut | 60 / 16.7 | 60 / 16.7 | 60 / 16.7 |
| cubestorm | 2 fps / p95 782.8 ms | 60 / 16.7 | 60 / 16.7 |
| crackberg | 12 fps / p95 92.4 ms | 60 / 16.7 | 60 / 16.8 |
| cityflow | 60 / 16.7 | 60 / 16.7 | 60 / 16.7 |
| noof | 60 / 16.8 | 60 / 16.7 | 60 / 16.7 |
| geodesic | 21 fps / p95 85.1 ms | 60 / 16.7 | 60 / 16.7 |
| gibson | 9 fps / p95 118.2 ms | 60 / 16.7 | 60 / 16.7 |

Not ported (upstream GLSL): hypertorus 60 / 16.8, klein 60 / 16.7, projectiveplane 60 / 16.7.

### PEGASUS (Intel UHD 630, Mesa iris, 1920x1080) - the weak class

| hack | legacy GL1 | classic | enhanced |
|---|---|---|---|
| voronoi | 60 / 16.9 | 60 / 16.9 | 45 fps / p95 22.3 ms |
| gravitywell | 60 / 16.9 | 60 / 16.8 | 60 / 16.9 |
| hexstrut | 60 / 16.9 | 60 / 16.9 | 60 / 16.8 |
| cubestorm | 28 fps / p95 37.6 ms | 60 / 16.9 | 60 / 16.9 |
| crackberg | 60 / 16.9 | 60 / 16.9 | 60 / 16.9 |
| cityflow | 60 / 16.8 | 60 / 17.0 | 60 / 17.0 |
| noof | 60 / 16.9 | 60 / 16.9 | n/a |
| geodesic | 60 / 19.5 | 60 / 16.9 | 60 / 16.9 |
| gibson | 60 / 16.8 | 60 / 17.0 | 60 / 16.9 |

Not ported (upstream GLSL): hypertorus 60 / 16.9, klein 60 / 16.9, projectiveplane 60 / 16.9.

### CHIMERA (AMD Radeon Pro 5500M / Navi14)

| hack | legacy GL1 | classic | enhanced |
|---|---|---|---|
| voronoi | 60 / 16.8 | 60 / 16.8 | 60 / 16.8 |
| gravitywell | 60 / 16.8 | 60 / 16.8 | 60 / 16.7 |
| hexstrut | 60 / 16.8 | 60 / 16.8 | 60 / 16.7 |
| cubestorm | n/a | 60 / 16.8 | 60 / 16.8 |
| crackberg | 45 fps / p95 25.2 ms | 60 / 16.7 | 60 / 16.7 |
| cityflow | 60 / 16.8 | 60 / 16.8 | 60 / 16.8 |
| noof | 60 / 16.7 | 60 / 16.8 | 60 / 16.8 |
| geodesic | 60 / 17.0 | 60 / 16.8 | 60 / 16.8 |
| gibson | 33 fps / p95 31.8 ms | 60 / 16.7 | 60 / 16.8 |

Not ported (upstream GLSL): hypertorus 60 / 16.7, klein 60 / 16.8, projectiveplane 60 / 16.8.

### MEDUSA (same hardware as CHIMERA)

| hack | legacy GL1 | classic | enhanced |
|---|---|---|---|
| voronoi | 60 / 16.8 | 60 / 16.8 | 60 / 16.8 |
| gravitywell | 60 / 16.8 | 60 / 16.8 | 60 / 16.8 |
| hexstrut | 60 / 16.8 | 60 / 16.8 | 60 / 16.8 |
| cubestorm | 30 fps / p95 33.6 ms | 60 / 16.7 | 60 / 16.7 |
| crackberg | 53 fps / p95 21.9 ms | 60 / 16.7 | 60 / 16.7 |
| cityflow | 60 / 16.8 | 60 / 16.8 | 60 / 16.8 |
| noof | 60 / 16.7 | 60 / 16.8 | 60 / 16.7 |
| geodesic | 60 / 17.1 | 60 / 16.7 | 60 / 16.7 |
| gibson | 36 fps / p95 30.3 ms | 60 / 16.7 | 60 / 16.7 |

Not ported (upstream GLSL): hypertorus 60 / 16.8, klein 60 / 16.8, projectiveplane 60 / 16.8.

### Class summary for tiers.tsv

* Weak class (Intel UHD 630): all nine ports hold 60 fps in `classic`; the GL1 cubestorm ran at 28 fps there.
  In `enhanced` everything holds 60 except voronoi (45 fps, per-pixel loop over all sites); noof enhanced is
  unmeasured on this host. `style=auto` therefore picks `classic` on the weak class. Suggested marks:
  weak-ok for every classic style, `enhanced` voronoi mid-class.
* Mali-G720 (O6N, MS-R1): the GL1 build was unusable for cubestorm (1.9 fps), gibson (9), geodesic (11 to 21),
  crackberg (12 to 14) and marginal for voronoi (35 to 56); every port holds 60 fps p95 <= 17.4 ms in both styles.
* Discrete AMD (CHIMERA, MEDUSA): everything holds 60 fps in every style.


## What changed per hack

| hack | approach | fidelity notes (intentional differences) |
|---|---|---|
| voronoi | classic: one instanced cone draw into the depth buffer (the original cone trick, one draw call instead of 25-125 `glBegin`s); enhanced: fragment shader over a site table (nearest and second nearest site) for anti-aliased borders, border glow, radial shading, sites fade in from the color of the cell they are born in | same simulation and colormap; the sites start clustered and the cells are large flat sectors until the first zoom, exactly as upstream (this is correct, not a bug). Marker size scales with render height. |
| gravitywell | height field evaluated in the vertex shader from a star table (no CPU grid); lines are triangle strips with an anti-aliased edge; foot circles as instanced loops | Upstream applies exp2 fog (density 0.005). The GL1 build in this catalog ignored fog, so the deep wells were bright; `--fog` (default none for classic, 0.5 for enhanced, 1 = upstream) exposes both looks. The far cutoff of each well is exact instead of a 16-sample linear approximation. |
| hexstrut | instanced struts, vertex shader builds the three quads of each triangle; rotator, propagation delay and colormap identical | enhanced adds edge AA, a brightness boost on turning struts and glow. |
| cubestorm | one static bevelled-cube mesh drawn instanced for every history cube (up to 1400) | per-pixel version of the GL1 light (ambient 0.2, diffuse, sharp cyan specular). enhanced: rim light, depth cue, older cubes fade. |
| crackberg | terrain generator, morphs, visibility walk and camera copied line for line; each tile is generated once into a static VBO (`cs_crackberg.c` contains upstream code, Matus Telgarsky) | the flat pale sky is `glClearColor` upstream and stays; enhanced adds a sky gradient, fog into the horizon color, fill light, glossy water, glowing lava. |
| cityflow | every tower is an instance with its own color, 800 towers (the compat build capped at 300) | fixes the flat single-color picture (see below). Light fixed to the model as upstream. enhanced: smooth (not integer-stepped) wave heights, street-level ambient occlusion, fog. |
| noof | persistent accumulation buffer that is only drawn into; simulation at the original 100 steps per second, several steps per frame | the per-frame `glCopyTexSubImage2D` of the GL1 version is gone. enhanced: anti-aliased glowing outlines and very slow trail fade so the picture does not clog. |
| geodesic | all four frequencies live in static instance buffers holding each face's corners both flat and on the sphere; the vertex shader morphs (one uniform) and builds the nine quads of a frame | mesh, solid, stellated, stellated2, random all supported; `wire` is not. enhanced: rim light, depth cue. |
| gibson | static instanced towers, faces, floor and text quads; the vertex shader places each text set on whichever tower currently owns it (uniform table), so the original random swapping is preserved | text is drawn with a built-in 5x7 bitmap font (no font dependency); upstream used a system font. Same strings, same layout logic. |
| hypertorus, klein, projectiveplane | not ported | upstream `HAVE_GLSL` code: static VBOs and a shader; 60 fps on every reference GPU (tables above). klein's close-up "walk" view mode is upstream behavior. |

## Why the GL1 build looked wrong (compat-layer findings)

* cityflow: `gles3_compat.c` keeps one material uniform per flushed batch, but cityflow calls
  `glMaterialfv` per tower inside a single `glBegin(GL_QUADS)`, so every tower took the last
  tower's color and the tops merged into one polygon.
* The compat layer stores `glLightPosition` raw (never transformed by the modelview current at
  the call), so back-lit hacks (cityflow, crackberg) got ambient only.
* crackberg: `GL_COLOR_MATERIAL` inside display lists collapses each row to one color;
  `glFrontFace` inside lists is not replayed.
* No fog, specular, `glPolygonMode`, smooth points/lines; state inside display lists.
* Every recorded batch replays as a full flush (index-buffer create/delete per QUADS): the
  reason cubestorm, gibson, geodesic and crackberg were CPU-bound.

## Copyright text for debian/copyright

```
Files: src/classics_shader/cs_voronoi.c src/classics_shader/cs_gravitywell.c
 src/classics_shader/cs_hexstrut.c src/classics_shader/cs_cubestorm.c
 src/classics_shader/cs_cityflow.c src/classics_shader/cs_geodesic.c
 src/classics_shader/cs_gibson.c
Copyright: 2003-2025 Jamie Zawinski <jwz@jwz.org>
           2026 Jason Perlow <jperlow@gmail.com> (shader-engine port)
License: xscreensaver and GPL-2+
Comment: Ports of the xscreensaver hacks of the same names; the simulation follows
 the original and keeps its permission notice. The GLES3 glue is GPL-2+.

Files: src/classics_shader/cs_crackberg.c
Copyright: 2005 Matus Telgarsky <catachresis@cmu.edu>
           2026 Jason Perlow <jperlow@gmail.com> (shader-engine port)
License: xscreensaver and GPL-2+
Comment: The terrain generator, morph state machine, visibility walk and camera are
 the unmodified upstream code; only the rendering is replaced.

Files: src/classics_shader/cs_noof.c
Copyright: 2004-2018 Bill Torzewski <billt@worksitez.com>
           (ported to xscreensaver by Jamie Zawinski, 2004)
           2026 Jason Perlow <jperlow@gmail.com> (shader-engine port)
License: xscreensaver and GPL-2+

Files: src/classics_shader/cs_common.h src/classics_shader/cs_post.h
Copyright: 2026 Jason Perlow <jperlow@gmail.com>
License: GPL-2+
```

## Fidelity review

Contact sheets (legacy GL1 build, `classic`, `enhanced`; three timestamps each, same colormap
family; captured on MEDUSA, Navi14, 1536x960) are in `docs/classics-shots/<hack>.jpg`. Honest
notes: in gravitywell and hexstrut the enhanced style is a refinement rather than a clear win
(softer edges, glow); voronoi, crackberg, cityflow, noof, geodesic and gibson gain most from it.
The `enhanced` voronoi costs a per-pixel loop over all sites and is therefore not a weak-class
style.

## Next batch (proposal, not started)

To be added from the ~170 upstream GL hacks after operator approval; each would follow the same
recipe (retained geometry, instancing, no fixed-function emulation). Candidates that suit the weak
class: `glschool`, `munch`, `cloudlife`, `discrete`, `thornbird`, `flame`, `coral`, `galaxy`,
`raverhoop`, `lavalite` (metaballs), `atlantis`, `hextrail`. Cost estimate per hack: 0.5 to 1 day
for the geometry-only ones, 1 to 2 days for the simulation-heavy ones; expected weak-class verdict
is ok for everything that is instanced meshes or line art, marginal for fill-heavy 2D hacks.

## Packaging note

New sources are built by the `classics_shader_ported` block in `meson.build`; no new runtime
files and no new dependencies (shaders are embedded). `-Dlegacy-classics` defaults to false.
Suggested next package version: 0.5.0.
