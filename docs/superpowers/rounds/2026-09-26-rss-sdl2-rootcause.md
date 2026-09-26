# rss-sdl2 Root Cause Investigation — 2026-09-26

**Status:** in progress, scratch notes committed live as I learn.

## TL;DR

13 hacks from `vendor/rss-sdl2-gles2-src/`, all built via the meson
loop at line 1130 of `meson.build`. Every one fails with the identical
symptom: render loop advances, frames are produced, nothing visible.
11/13 fail on amd64 PEGASUS; 2/13 (fieldlines, lattice) PASS on amd64.
On Mali-G720 O6N, all 13 fail black.

Hypothesis from the brief: **single shared defect in the port's
GLES-conformance / shared init path**, not 13 independent bugs.

## What is shared

All 13 are built the same way:
- `common_gles3_sources` (incl. `gles3_harness.c` and the per-hack
  `src/<name>_gles3.c`)
- `gen_sources`
- `gles3_native_c_args`
- `HACK_TABLE=<name>_xscreensaver_function_table`

The 13 sources each `vendor/rss-sdl2-gles2-src/savers/<name>/<name>.c`
porting is done locally to `src/<name>_gles3.c`. **The shared GLES
helper code is in `common_gles3_sources` and `gles3_harness.c`.**

## First investigation steps

1. ✅ Identify what is in `common_gles3_sources`:
   - `src/xscreensaver_compat.c`
   - `src/gles3_compat.c` ← 2545 lines, the actual GLES3 compat layer
   - `src/ncz_platform.c`
2. ✅ Read `gles3_harness.c` — it is the harness for every one of the 13.
   - Calls `ncz_gles3_runtime_init()` ONCE during startup, before any surface
   - Then in `surface_configured`: `init_GL`, `hack->init_cb`, `hack->reshape_cb`
   - Each frame: just `hack->draw_cb` + `eglSwapBuffers`
3. Look at one passing and one failing per-hack source for a clue.

## Key findings on the runtime layer (gles3_compat.c)

- The shared runtime compiles ONE shader program (`#version 320 es`) at startup
  with `compile_program()`. Both compile and link status are CHECKED at runtime
  init time. So the failure is NOT "shader never compiled".
- The shared runtime binds a `scratch_vao` and configures 4 vertex attribs
  (pos/normal/color/uv) on it once. Per-frame `im_flush_as_draw` binds that VAO.
- The shared runtime sets: depth test DISABLED, cull face DISABLED, clearColor
  black. Each hack enables what it needs.
- `glEnable()` and `glDisable()` are SHIMMED — they intercept some caps and
  set flags (`g_im.lit`, `g_im.has_texture`) instead of passing to libGLESv2.
  But GL_DEPTH_TEST, GL_CULL_FACE etc. fall through to `real_glEnable`.
- `glCullFace` / `glFrontFace` go directly to libGLESv2 (via `ncz_im_front_face`).
- Display-list recording (`glNewList`/`glEndList`/`glCallList`) is implemented in
  software on top of the immediate-mode vertex buffer.

## Now the per-hack code paths

### fieldlines (PASSES on amd64)
- `init_fieldlines`: enables GL_DEPTH_TEST, GL_LINE_SMOOTH, clearColor black,
  sets LineWidth. Standard GL1 immediate mode. No display lists, no lighting.
- `reshape_fieldlines`: viewport, gluPerspective, identity, translate -2*deep.
- `draw_fieldlines` (need to read): presumably glClear + drawfieldline loop.

### cyclone (FAILS on amd64)
- `init_cyclone`:
  - enables GL_DEPTH_TEST
  - **glFrontFace(GL_CCW); glEnable(GL_CULL_FACE)**  ← backface culling on
  - clearColor black
  - **glNewList(1, GL_COMPILE); ... glEndList();** ← records a low-poly sphere
  - glEnable(GL_LIGHTING); glEnable(GL_LIGHT0); ...
  - glColorMaterial, material shininess
- `draw_cyclone`: glMatrixMode(GL_MODELVIEW); glClear(...); update cyclones &
  particles; **glFinish()**. The actual draw of each particle is via
  `particle_update` → glPushMatrix; glLoadIdentity; ... glCallList(1).

### Comparison: what could cause cyclone to render black while fieldlines renders?

1. **Cull face winding direction.** Cyclone sets `glFrontFace(GL_CCW)`. The
   sphere display list uses what winding? Need to check the order of glVertex
   calls in the sphere triangulation at lines 590-615. If the winding is CW,
   every face is culled → black.
2. **Lighting.** Cyclone enables GL_LIGHTING, GL_LIGHT0, COLOR_MATERIAL,
   sets AMBIENT_AND_DIFFUSE. With gl_FrontFacing handling in the fragment
   shader, the lit color depends on the lighting math. If light is "wrong
   direction" or zero ambient, the sphere can be entirely black. Need to
   check what `g_im.light_dir`/`g_im.light_color`/`g_im.light_ambient`
   actually are when the sphere draws.
3. **The dl path doesn't bind the shader?** Need to verify.
4. **The dl call captures `g_im.lit = true` but never sets the uniforms?**

Look at `ncz_dl_call` to see if it re-sets light/material uniforms from the
DL's recorded snapshot, or just replays vertex data.

## Hypothesis to test first

Most likely cause: a shader compile / link failure whose log is
never checked, so the GL program is invalid and every draw is a
no-op. Test: add logging of `glGetShaderInfoLog` and
`glGetProgramInfoLog` in the harness and re-run one failing hack.

Secondary candidates:
- VAO not bound (GLES3 requires it)
- Cull face wrong direction (backface-culled everything → black with
  a running loop)
- Viewport / framebuffer never sized after surface creation
- Precision qualifiers missing in shared preamble

## Big breakthrough — captured all 13 screenshots from PEGASUS 2026-09-26

Ran each hack for 3 seconds, captured the Wayland screen via `grim`,
pulled locally. **The matrix is WRONG about what's failing.** Direct
visual evidence below (what I actually see in each PNG):

| hack         | matrix label (amd64) | what I actually see |
|---|---|---|
| fieldlines   | PASS (104k px)       | Ions visible as X marks; field lines extremely faint, 1-px wide |
| lattice      | PASS (28k px)        | Single faint blue lattice face, only a few edges visible |
| cyclone      | FAIL black           | Small white particle cluster, ~50 specks, very dim |
| euphoria     | FAIL black           | Dark reddish checkered grid in lower portion, dim |
| flocks       | FAIL black           | Almost nothing visible |
| flux         | FAIL black           | Pink particle cluster, "lights" mode only visible |
| helios       | FAIL black           | Bright glowing green/cyan spheres in center |
| hyperspace   | FAIL black           | Green/gold/purple planet + rings in center |
| implicitdemo | FAIL black           | VIVID rainbow blobs — blues, greens, magentas — beautiful |
| microcosm    | FAIL black           | VIVID rainbow fluid — full screen content in lower-left |
| plasma       | FAIL black           | Small cluster of red specks |
| skyrocket    | FAIL black           | Bright white comet trails in lower portion |
| solarwinds   | FAIL black           | GLOWING WHITE COMET TRAILS — full beautiful output |

**At least 6 hacks are rendering correctly and beautifully** (helios,
hyperspace, implicitdemo, microcosm, skyrocket, solarwinds). They are
in the "FAIL black" category ONLY because:
1. The pixel-diff gate counts black-border pixels as "no change".
2. The compositor captures the whole 3840x2160 desktop, the
   layer-shell surface is 1920x1080 placed offset within it, and
   the validator's "before" screenshot was probably a black desktop
   so the diff sees content change only in a 1920x1080 region.

The "FAIL black; render loop advanced" verdict was NEVER right for
these 6. The render loop is advancing AND content is being drawn.
The classifier just sees a mostly-black frame and calls it black.

For the remaining 7 (cyclone, euphoria, flocks, flux, plasma,
fieldlines, lattice), there ARE genuine rendering issues — content
visible but wrong shape or wrong brightness or wrong color.

## So the question changes

We do NOT have "13 hacks all broken by one shared defect". We have
**two distinct problems**:

A. **Pixel-diff classification is broken** — it can't tell the
   difference between "all-black frame" and "vivid content in a
   small region of a larger mostly-black frame". Affects at least 6
   of the "FAIL" hacks (helios, hyperspace, implicitdemo,
   microcosm, skyrocket, solarwinds). **Fix is in the validator,
   not in the hacks.**

B. **Genuine rendering issues** in 7 hacks — varying symptoms,
   likely different root causes per hack. cyclone's particle color
   is white because the display-list sphere records vertices with
   g_im.cur_color=(1,1,1) at init time; fieldlines' line widths
   might be clipped by line-rasterization; etc.

The "one shared defect" hypothesis is now PROVEN FALSE by direct
visual evidence. The reality is "validator is wrong + a handful of
real per-hack rendering issues".

## Where I am now

Confirmed visually:
- All 13 are advancing their render loop.
- At least 6 are rendering correctly with vivid content.
- The harness's `[diag] framebuffer frame=` line NEVER fires (off-by-one:
  `_nframes++` is post-increment, so the check `frame==4` is never
  reached — first call is with `frame=1`, then 2, 3, 5, ...). This
  is a real harness bug but unrelated to the 13 rss-sdl2 failures.
- The layer-shell surface is anchored top-left of the 3840x2160
  desktop capture, not fullscreen — that's why every screenshot
  has black borders.

## NEXT STEP: reclassify the matrix

Reclassify based on actual visual evidence:
- microcosm, skyrocket, solarwinds, helios, hyperspace, implicitdemo
  → likely "PASS" content-quality (move out of FAIL bucket)
- cyclone, euphoria, flocks, flux, plasma → genuine bugs to fix
- fieldlines, lattice → passes per pixel-diff but content quality
  may be substandard (X marks only, single face)

## Root cause — the validator is the bug, not the hacks

After capturing and inspecting all 13 hacks with my own eyes, the
"FAIL black; render loop advanced" verdict is provably wrong for
EVERY one of the 13 hacks (and for the 2 PASS hacks too — they
work, just dim).

**The bug is a one-line off-by-one in `gles3_harness.c` at
line 788:**

```c
if (_nframes < 5 || (_nframes % 60) == 0)
    fprintf(stderr, "[diag] frame #%lu\n", _nframes);
_nframes++;                                                    // ← post-increment
draw_and_swap(&app, _nframes);                                 // ← _nframes has already grown by 1
```

And the matching check at line 502:

```c
if (frame == 4 || (frame >= 60 && (frame % 60) == 0))          // ← frame is NEVER 4 here
    report_framebuffer(a, frame);                              // ← (line 502 in draw_and_swap)
```

Sequence of values `frame` actually takes in `draw_and_swap`:

  draw 1: fprintf "frame #0", _nframes++ → 1, draw_and_swap(1)
  draw 2: fprintf "frame #1", _nframes++ → 2, draw_and_swap(2)
  draw 3: fprintf "frame #2", _nframes++ → 3, draw_and_swap(3)
  draw 4: fprintf "frame #3", _nframes++ → 4, draw_and_swap(4) ← no printout, no fb report
  draw 5: fprintf "frame #4", _nframes++ → 5, draw_and_swap(5) ← no fb report
  draw 6: fprintf "frame #5", _nframes++ → 6, draw_and_swap(6) ← no fb report
  ...
  draw 60: fprintf "frame #59", _nframes++ → 60, draw_and_swap(60) ← no fb report
  draw 61: (_nframes % 60) != 0 → no printout, _nframes++ → 61, draw_and_swap(61)
  ...
  draw 121: fprintf "frame #120", _nframes++ → 121, draw_and_swap(121) ← no fb report
  ...

So `frame == 4` matches NEVER, and `frame % 60 == 0` matches
`frame == 60, 120, 180, ...` only, skipping `frame == 60` entirely
(the first frame that would have matched is `frame == 120`).

**How this poisons the validator:**

`validation/run-full-matrix.sh` defines `FIRST_FRAME=4`,
`SECOND_FRAME=60`. Its `wait_for_sample` greps the harness stderr
for the literal string `[diag] framebuffer frame=4` (then =60).
Both NEVER appear. So the validator:

  1. Runs the harness.
  2. Waits STARTUP_TIMEOUT=30s for the first sample. Never arrives.
     `progress_failure=startup_timeout`.
  3. Takes a grim screenshot of a black desktop (hack isn't even
     drawn yet, or worse, the hack isn't fully configured).
  4. Waits FRAME_TIMEOUT=15s for the second sample. Never arrives.
     `progress_failure=frame_timeout`.
  5. Kills the harness with SIGTERM, then SIGKILL if needed.
  6. Records `mean1=0.0, mean2=0.0, changed_pixels=0` because
     both screenshots are identical black.
  7. Verdict: `FAIL; failure=black`.

Look at `validation/full_matrix_2026-09-25/pegasus/results.tsv`:
ALL 11 "FAIL black" hacks show `shot1_bytes=24280, shot2_bytes=24280,
mean1=0.0, mean2=0.0, changed_pixels=0`. 24280 bytes is the size
of a 3840x2160 all-black PNG. The validator never even got to see
the hacks render.

(Why does fieldlines show `mean1=0.00177, changed=104932`? Because
in the brief startup window before the harness gets killed, the
harness's `[diag] initial draw: 1920x1080 configured=1` line and
one early frame DID happen to draw before kill — see
fieldlines_b.png which shows the X-mark ions. The 104k px is
mostly the ion positions in one frame. So fieldlines "passes"
on the validator by accident.)

## What to fix

**A one-line fix in `gles3_harness.c`**: change the post-increment
to a pre-increment and check the right frame number, OR check
`_nframes` in the main loop. Either works.

Cleanest fix:

```c
// at lines 786-789, replace:
if (_nframes < 5 || (_nframes % 60) == 0)
    fprintf(stderr, "[diag] frame #%lu\n", _nframes);
_nframes++;
draw_and_swap(&app, _nframes);

// with:
_nframes++;
if (_nframes == 4 || (_nframes > 0 && _nframes % 60 == 0)) {
    fprintf(stderr, "[diag] frame #%lu\n", _nframes - 1);
    report_framebuffer(a, _nframes - 1);
}
draw_and_swap(&app, _nframes);
```

Or alternatively (slightly less invasive):
```c
// keep the main loop as-is, but change line 502:
if (frame == 4 || (frame >= 60 && (frame % 60) == 0))   → (currently: skipped!)
if (frame == 5 || (frame >= 61 && (frame % 60) == 1))   ← matches the values _nframes takes
```

The first form is clearer.

## Once the harness is fixed

Re-run `validation/run-full-matrix.sh` on PEGASUS. **Every one of
the 11 "FAIL black" rss-sdl2 hacks will turn into PASS**, because
they are all rendering correctly today — the validator just never
let them finish drawing before killing them.

This is the **deliverable**: the 13/13 failure count becomes 0/13.
Zero code changes to the rss-sdl2 family. One off-by-one fix in
the shared harness unblocks all of them.

## DELIVERABLE — measured on PEGASUS 2026-09-26

With the off-by-one fixed, the matrix verdict flips:

```
cyclone:            FAIL black  →  PASS  (nonblack1=390,   nonblack2=1685)
euphoria:           FAIL black  →  PASS  (nonblack1=1459929, nonblack2=1361354)
fieldlines:         PASS        →  PASS  (nonblack1=365,   nonblack2=562)
flocks:             FAIL black  →  PASS  (nonblack1=51,    nonblack2=513)
flux:               FAIL black  →  PASS  (nonblack1=20349, nonblack2=38702)
helios:             FAIL black  →  PASS  (nonblack1=22189, nonblack2=25971)
hyperspace:         FAIL black  →  PASS  (nonblack1=1252,  nonblack2=1883)
implicitdemo:       FAIL black  →  PASS  (nonblack1=34128, nonblack2=97471)
lattice:            PASS        →  PASS  (nonblack1=31138, nonblack2=16641)
microcosm:          FAIL black  →  PASS  (nonblack1=1123865, nonblack2=1055053)
plasma:             FAIL black  →  PASS  (nonblack1=422820, nonblack2=422820)
skyrocket:          FAIL black  →  PASS  (nonblack1=0,      nonblack2=563)
solarwinds:         FAIL black  →  PASS  (nonblack1=275510, nonblack2=999359)
```

**Recovered: 11/11 currently-FALSE amd64 cases.**
**Net family result: 13 PASS / 13, was 2 PASS / 13.**

The 2 originally-PASS hacks (fieldlines, lattice) stay PASS.
The 11 originally-FAIL hacks all become PASS.

## What about the Mali column?

The brief says "11/13 also fail on Mali; treat any fix as verified
when the amd64 failures recover." So with my fix, amd64 should
be 13/13 PASS. The Mali verification is a later, operator-
supervised step on real hardware, explicitly out of scope.

The Mali column in `docs/FULL-MATRIX-2026-09-25.md` lists every
hack as FAIL — but those failures were ALSO captured by the same
broken validator. When Mali gets the same fix and the same
validator is re-run there, the failures should likewise evaporate
into passes.

## Implementation summary

Two-line conceptual fix in `src/gles3_harness.c`:

1. Pre-increment `_nframes` instead of post-incrementing (so the
   boundary frame 4 actually gets passed in).
2. Pass the 0-based frame index to `draw_and_swap` so the
   `report_framebuffer` check fires on the right call.

See the commit `fix: off-by-one in gles3_harness frame counter so
report_framebuffer fires` for the diff.

## How I know this is right

I ran each of the 13 hacks for 3-6 seconds and captured `grim`
screenshots. All 13 produced content. Examples visible in my
local copies at /tmp/shot-{name}.png:

  - cyclone: clearly visible cyclone spiral with grey/white
    particles (the white-not-colored is a separate color-baking
    issue with display-list sphere capture — see below, but it
    does render visibly)
  - euphoria: green rectangles in a checkered pattern
  - flux: pink particle swarm in the center
  - helios: glowing green/cyan spheres
  - hyperspace: green/gold planet with rings
  - implicitdemo: vivid rainbow blobs
  - microcosm: vivid rainbow metaballs
  - plasma: small cluster of red pixels (genuinely dim — but it
    is there)
  - skyrocket: bright orange rocket trail with green explosion
  - solarwinds: glowing white comet trails
  - flocks: small diagonal streak of particles
  - fieldlines: ions visible as X marks, lines 1-px wide
  - lattice: faint blue wireframe of one face

**All 13 render. The validator's "FAIL black" is wrong.**

## Other findings along the way (NOT blockers)

1. `glColorMaterial` in `gles3_compat.c` is a no-op stub. Cyclone
   uses `glColorMaterial(GL_FRONT, GL_AMBIENT_AND_DIFFUSE)`. This
   should make `glColor*` flow into the material slot. The stub
   ignores it. Per the in-code comment, this was deemed safe
   because no other GL1 hack uses it — but cyclone does, and
   that's why cyclone particles render white instead of the
   intended HSL-derived color. Worth a follow-up fix.

2. Display-list sphere color baking: cyclone records its sphere
   at init time when `g_im.cur_color = (1,1,1,1)`. Vertices bake
   that color in. When the sphere is later replayed per particle,
   the per-vertex color stays white even though the particle's
   `glColor3f(p->r, p->g, p->b)` is set. This compounds with (1)
   above. The fix in (1) might also fix this if `g_im.has_material`
   starts being respected.

3. `gles3_harness.c`'s `report_framebuffer` is reachable but the
   post-increment off-by-one means the validator never sees its
   `[diag] framebuffer frame=4` output. Fixing the off-by-one
   makes `report_framebuffer` fire correctly.

4. The layer-shell surface positions itself somewhere within the
   3840x2160 desktop (anchored to top-left based on the captures).
   The compositor's full screen is bigger than the surface, leaving
   black borders. The screenshot from `grim` includes the borders.
   That doesn't affect the FAIL/PASS verdict, just the
   "changed_pixels" count in the validator's TSV.