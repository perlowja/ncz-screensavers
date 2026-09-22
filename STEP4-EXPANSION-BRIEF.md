# Three real expansions: xscreensaver coverage gap, hyprsaver port, RSS-GLX port

## Context

Repo `ncz-screensavers`, branch `master`, current HEAD after `b4ea803`
(includes the frame-callback fix, eglSwapInterval fix, and real
cross-platform validation results: 86/90 PASS on all 3 platforms, 4 real
failures being tracked separately in `STEP3-CRASH-FIXES-BRIEF.md` — do NOT
duplicate that work, it's a parallel dispatch).

Three real, separate expansion tasks, in priority order. Each should be its
own commit series, don't tangle them together.

## Task 1a: two named, confirmed-real gaps — port these first

The operator named 4 specific hacks. Checked against this repo's own
tracking (grep meson.build + PORTED.md) before dispatching, so this isn't
guesswork:

- **starwars** — already done. GLES3-native, in `meson.build`'s
  `gles3_native` list (`starwars_gles3`). Do NOT re-port, do NOT spend any
  time on it, this is just a heads-up so it isn't rediscovered as a "gap."
- **pinion** — already correctly deferred with a real, specific, previously
  investigated blocker (PORTED.md ~line 452): GL1 selection-mode picking
  API (`glInitNames`/`glPushName`/`glPopName`/`glRenderMode`/
  `glSelectBuffer`) plus `gluPickMatrix`, none of which exist in GLES3 —
  the architectural blocker is a real render-to-texture hit-test redesign,
  not a quick shim. Do NOT attempt this in this pass unless you have a
  genuinely new approach beyond what's already documented there — read
  that PORTED.md entry FIRST if tempted to touch pinion at all.
- **atlantis** and **flurry** — genuinely NOT tracked anywhere in this
  project (confirmed absent from meson.build and PORTED.md). Real gaps.
  Port both using this project's established recipe (vendor the upstream
  `.c`/companion files from `/tmp/xscreensaver-upstream-check` on
  ACHILLES, swap `xlockmore.h` for `xscreensaver_compat.h`, one meson row,
  fix whatever compat-shim gaps the compiler surfaces — same iterative
  process PORTED.md documents for every other hack). `flurry` has
  `flurry-smoke.c`, `flurry-spark.c`, `flurry-star.c`, `flurry-texture.c`
  as real companion files (already seen in the upstream file listing) —
  vendor all of them together, they're one hack's supporting objects, not
  separate screensavers.

## Task 1b: broader xscreensaver coverage gap analysis

`~/Projects/ncz-screensavers`'s tracked hack set (from `meson.build`'s
`ported_hacks`/`legacy_gles3_hacks`/`gles3_native` tables) needs a real
diff against upstream's actual REGISTERED hack list — NOT the raw
`hacks/glx/*.c` file list (that includes shared model/helper sub-files like
`cow_hide.c`, `toaster_wing.c`, `flurry-smoke.c` that are part of an
ALREADY-ported hack, not separate screensavers, and diffing against it
produces false positives).

The real upstream source of truth: `hacks/config/*.xml` in
`https://github.com/Zygo/xscreensaver` (already cloned at
`/tmp/xscreensaver-upstream-check` on ACHILLES — reuse it, don't re-clone;
318 real registered hacks found there, covers 2D X11 hacks too, not just
GLX/GL ones — this project only tracks the GL ones so far, confirm that
scoping is still the right call or note if 2D hacks should be considered
separately).

Produce a real, clean diff: registered upstream hacks minus everything this
project's PORTED.md/meson.build already tracks (ported, deferred, OR
explicitly excluded with a stated reason). For anything genuinely missing
and never previously considered, port it using the exact same recipe this
project's own PORTED.md documents (the "port recipe" section — vendor the
`.c`, swap `xlockmore.h` for `xscreensaver_compat.h`, one meson row,
link-gate). Don't force every single upstream hack to be ported if it hits a
real documented blocker class already known (GLU tessellator, threading,
etc. — see PORTED.md's existing Deferred section for the pattern) — flag
those the same way the existing 4 deferred hacks are flagged, with a real,
specific technical reason.

## Task 2: port `vendor/hyprsaver`'s 33 GLSL shaders as native hacks

`vendor/hyprsaver/shaders/*.frag` (33 files: aurora, attitude, bezier, blob,
caustics, circuit, clouds, donut, fireflies, flames, fractaltrap, geometry,
gridwave, hypercube, julia, kaleidoscope, lissajous, marble, matrix,
mobius, oscilloscope, planet, plasma, shipburn, snowfall, sonar,
starfield, stonks, temple, terminal, tesla, tunnel, voronoi, waterfall,
wormhole) is a SEPARATE, MIT-licensed project (a Rust/wgpu Hyprland
screensaver, own EGL/Wayland stack in `src/`) already vendored into this
repo. Its shaders already target `#version 320 es` (GLES 3.2) — this is
dramatically easier than the legacy xscreensaver ports, since there's no
immediate-mode GL emulation needed at all.

**Do NOT try to reuse hyprsaver's own Rust binary/renderer.** Instead,
write ONE generic C wrapper for `gles3_harness.c`'s hack interface (an
`init_cb`/`draw_cb`/`free_cb` triple) that:
- Renders a fullscreen textured quad (2 triangles, standard NDC
  `[-1,1]x[-1,1]`).
- Compiles whichever `.frag` file the meson row names, paired with a
  trivial pass-through vertex shader.
- Feeds `u_time` (seconds since start), `u_resolution` (viewport size in
  pixels), `u_frame` (frame counter) — check each shader's actual uniform
  declarations (`grep -h '^uniform' vendor/hyprsaver/shaders/*.frag | sort
  -u` to get the real complete set across all 33 — don't assume they're
  identical, some may have extras like `u_mouse` which this headless/
  screensaver context has no real value for; default it to a fixed point,
  don't invent fake motion).
- One meson row per shader, all sharing the SAME generic wrapper .c file
  (parameterized by which `.frag` to load) rather than 33 near-duplicate
  hack files — this is a good case for meson's `foreach` pattern already
  used elsewhere in this file for the table-driven hack list.

Real per-shader verification: build all 33, run each for a real several-
second duration on a live Wayland session, confirm real animation (same
`frame #N` diagnostic + real screenshot evidence this project already uses
everywhere), not just "it compiled."

License note: hyprsaver's LICENSE (MIT, copyright Mara Vexa) must be
preserved/attributed per its terms — copy the LICENSE file into wherever
these shaders land in the built tree/package, and credit hyprsaver by name
in PORTED.md's entry for these, the same way the ffmpeg AV1 patch's README
in `cix-installer` credits its original author. Do not represent this work
as originally written for ncz-screensavers.

## Task 3: fetch and scope-port RSS-GLX (Really Slick Screensavers)

`https://rss-glx.sourceforge.net` — a real, separate, well-known GPL GLX
screensaver collection (Euphoria, Hyperspace, Skyrocket, Solarwinds, Biof,
Fieldlines, Flocks, Helios, Lattice, and others — these are real 3D GL
effects using immediate-mode/fixed-function OpenGL, like the original
xscreensaver hacks, not like hyprsaver's modern shaders).

1. Find the actual current source — SourceForge's project page should link
   the real repo/tarball (or check if it's mirrored on GitHub, several
   community mirrors exist; verify whichever you use is a genuine,
   unmodified copy of the real project, not a fork with unrelated changes).
2. Check its license (historically GPL) and confirm compatibility with how
   this project already handles GPL-licensed vendored xscreensaver hacks
   before vendoring anything.
3. Scope real effort BEFORE porting: RSS-GLX hacks are architecturally
   similar to xscreensaver's own GLX hacks (immediate-mode GL, custom
   per-hack main loops) but are a DIFFERENT codebase with their own
   internal API (not `xlockmore.h`-based) — the existing
   `xscreensaver_compat.h` shim likely does NOT directly apply. Read 2-3
   representative RSS-GLX hack source files first and report honestly
   whether the existing compat-shim recipe transfers, needs extension, or
   needs a second, RSS-GLX-specific shim — do not assume it's a drop-in
   repeat of the xscreensaver port recipe just because it looks similar on
   the surface.
4. Port however many are genuinely tractable in this pass; for anything
   blocked, document the real blocker the same way PORTED.md already does
   for xscreensaver's own deferred hacks.

## Hard constraints (apply to all 3 tasks)

- Real per-hack verification: build, run several real seconds under a live
  Wayland session, confirm real rendered/animating output (screenshot +
  frame-count diagnostic), not just a clean compile.
- Real license attribution for anything vendored from outside this
  project's existing 90-hack xscreensaver base (hyprsaver, RSS-GLX).
- Commit incrementally — one logical group per commit (e.g. all of Task 2's
  wrapper infrastructure in one commit, then batches of shaders, not one
  giant commit for all 33+any RSS-GLX ports).
- Push to `ssh://root@192.168.207.101/mnt/datapool/git/ncz-screensavers.git`
  (the `jasonperlow@` form is pubkey-only and will fail). Re-fetch and
  rebase before every push — this repo has had real concurrent-push
  collisions this session, including one from another zoder dispatch
  working on the crash fixes in parallel.
- Do NOT touch the 4 hacks being fixed in `STEP3-CRASH-FIXES-BRIEF.md`
  (jigsaw, hexstrut, highvoltage, mapscroller) — that's a separate,
  parallel dispatch on the same repo.

## Verification setup — TYDEUS pair

Author with MiniMax-M3, review with TYDEUS's local reviewer slot
(`--reviewer reviewer`, re-probe `http://192.168.207.73:8006/v1/models`
before trusting a cached claim about which model backs it — it rotates).
Wire a real `--check` (e.g. a script that builds every new target and greps
each one's stderr for real `GL_VERSION=`/frame-progress evidence over a
timed run — same shape as `STEP3-CRASH-FIXES-BRIEF.md`'s check). `--agent-
timeout 7200`, `--loop-timeout 10800` — this spans 3 real sub-tasks, budget
generously, and consider running it as `zoder loop` with multiple
iterations rather than expecting one pass to finish everything.
