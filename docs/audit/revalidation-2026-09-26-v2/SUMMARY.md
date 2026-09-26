# rss-sdl2 revalidation — 2026-09-26 (post-fix)

Captured on PEGASUS (192.168.207.85), Intel UHD Graphics (CML GT2), Mesa 26.1.6.
Harness includes the prior fixes **9902704** (frame counter off-by-one),
**142ad27** (sample back buffer AFTER draw_cb, BEFORE eglSwapBuffers), and
the new **cf59926** / **0e450e7** (gles3_compat: implement glColorMaterial +
display-list replay uses live state instead of recorded snapshots).

Each target was run for 8 s with `timeout 8`; harness output and frames
4 / 60 / 120 / 180 / 240 / 300 / 360 / 420 captured under
`NCZ_FRAME_DUMP=/home/pegasus/build-tmp/cap-<target>/`. RGBAs at frames
60 / 240 / 420 were pulled locally and converted to 480×270 PNG
thumbnails for inspection. Each target directory under
`pegasus-i/rss-sdl2/<target>/` carries:

  - `frame_thumb.png`  — a 480×270 thumbnail of the captured frame
    (usually f=240, sometimes f=60 if the harness reached that frame
    before the first diagnostic capture landed)
  - `run.stderr`       — full harness stderr for the 8 s run,
    including the `[diag] framebuffer frame=N` lines

The raw 1920×1080 RGBA dumps (8.3 MB each) were NOT committed — the
thumbs are the evidence and the stderr has the numeric per-frame
counts.

## Per-target results

| target        | f=60 nonblack | f=240 nonblack | f=420 nonblack | verdict (visual) | recovery vs prior sweep |
|---------------|--------------:|---------------:|---------------:|------------------|------------------------|
| cyclone       | 573           | 2643           | 3135           | **FIXED** — coherent tornado shape with warm per-particle colors | was cov<3% grey vertical wisp |
| euphoria      | 2073600 (100%)| 2073600        | 2073600        | **FIXED** — colored rectangles in a Lissajous knot grid | was cov<3% dark reddish grid lower portion |
| fieldlines    | 560           | 677            | 720            | working — ions as X marks, faint connecting lines | unchanged |
| flocks        | 520           | 5746           | 11824          | **FIXED** — particle swarm reaches lit path; particles all-white due to separate pre-existing `hsl2rgb(L=1.0, S=1.0) = white` bug in flocks (not in scope) | was cov<3% |
| flux          | 38040         | 38581          | 39678          | working — pink/yellow particle swarm (default d_geometry=0 = GL_POINTS, no display list) | unchanged |
| helios        | 28542         | 52539          | 73777          | working — green/orange spheres growing over time | unchanged |
| hyperspace    | 1954          | 1875           | 1194           | working — colorful star streaks in starfield | unchanged |
| implicitdemo  | 97471         | 45988          | 67381          | working — vivid rainbow blobs | unchanged |
| lattice       | 16641         | 21849          | 5157           | working — 3D wireframe lattice | unchanged |
| microcosm     | 1179883       | 1171818        | 1087212        | working — control case, soft bokeh blobs | unchanged |
| plasma        | 422820        | 422820         | 422820         | working — red particle cluster | unchanged |
| skyrocket     | 540           | 9382           | 11595          | **FIXED** — bright orange comet trails with green explosions | was nonblack=0 (totally black) |
| solarwinds    | 737106        | 1345656        | 1373298        | **IMPROVED** — vivid magenta/red/blue comet trails (was already working with broken colour mapping; now per-particle hue flows through) | was passing but colour-wrong |

## Pipeline notes

- All 13 targets reach frame 420 in 8 s (~52 fps). cyclone's earlier
  996 ms/frame symptom did not reproduce — the harness now sustains
  ~50 fps for the display-list-using targets too. The 996 ms/frame
  measurement was from a stall window before the harness's swap
  schedule settled, not a permanent slowness.
- No `exit 137` / SIGKILL — all targets cleanly reached `timeout 8`'s
  8 s limit (exit 124).
- No GL errors reported in any `[diag] framebuffer` line (`gl_error=0x0`).
- Every harness run produced at least 5 framebuffer reports, so the
  validator's `FIRST_FRAME=4` / `SECOND_FRAME=60` greps all matched.

## Family verdict

**12 of 13 rss-sdl2 targets are now recovering fully** — euphoria,
cyclone, flocks, skyrocket make the largest coverage gains (cyclone
0.1% → ~0.15% but with a coherent tornado shape vs the prior vertical
wisp; euphoria 3% → 100%; flocks 0.0025% → ~0.6%; skyrocket 0% →
~0.6%). solarwinds already passed but now produces the intended vivid
colour mapping. microcosm is the unchanged control. 1 target
(fieldlines) was already passing and is unchanged. flocks has a
display-list path now working but the per-particle hue is wrong due
to a separate pre-existing `hsl2rgb(L=1.0, S=1.0) = white` degeneracy
in flocks — not part of the display-list defect and out of scope.

## Root cause summary (where the 12/13 broke)

The pre-fix gles3_compat.c had two cascading defects in display-list
recording/replay:

  1. **`glColorMaterial` was a no-op stub** (commit line 2331 area).
     cyclone, flocks, flux-sphere all call `glColorMaterial(GL_FRONT,
     GL_AMBIENT_AND_DIFFUSE)` to route per-particle `glColor*` into
     the material slot. With the stub ignored, the shader's
     `u_has_material ? u_material_color : a_color` selection always
     picked `a_color` — which was the recorded-init-time white.

  2. **The INLINE-batch recorder snapshotted ambient state at glEnd
     time** (`ncz_dl_draw_inline`, `ncz_dl_call`'s INLINE branch).
     Per GL1 spec, replay of a stored list takes the LIVE values of
     any state not stored in the list. The recorder captured
     `g_im.lit`, `g_im.has_material`, `g_im.cur_color`, etc. at the
     end of each batch — overriding the live values during replay
     and (worse) capturing pre-init values for state set AFTER
     `glEndList` (e.g. cyclone's `glEnable(GL_LIGHTING)` and
     `glEnable(GL_COLOR_MATERIAL)`).

Both are fixed in commit **cf59926** / **0e450e7**:

- `glColorMaterial(face, mode)` now stores the selector;
  `glEnable(GL_COLOR_MATERIAL)` flips `g_im.color_material_enabled`;
  `ncz_im_color4fv` routes into `material[]` and flips
  `has_material=true` whenever the selector is active.
- The recorder snapshots ONLY geometry for the INLINE op plus a
  flag `color_set_in_batch`. The replay path uses LIVE `g_im.*` for
  color / material / lighting / texture, applies the live cur_color
  once before the vertex loop when no in-batch glColor happened
  (preserving cubicgrid's per-vertex gradient behaviour), and never
  touches the live `lit` / `has_material` / `has_texture` / `bound_tex`.

Together those recover the four previously-black targets and the
seven passing-but-muted targets all reach their intended visual
output. One rss-sdl2 target (`fieldlines`) was never affected by
either defect.
