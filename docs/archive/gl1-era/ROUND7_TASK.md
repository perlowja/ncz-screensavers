Round 7: build the MECHANICAL porting pipeline for xscreensaver's GL hacks — not a one-off hand-reimplementation. You are on macOS, cannot build; the coordinator builds on Debian forky arm64 and runs on real hardware (labwc 0.9.5/wlroots 0.20.2, Mali-G720-Immortalis/Mesa panfrost). CONFIRMED via eglinfo on real hardware: this GPU/driver advertises ONLY OpenGL ES 3.2 — no desktop OpenGL profile at all. That rules out linking xscreensaver's hacks against real libGL unchanged.

## The actual goal
xscreensaver ships ~200 GL hacks (glmatrix, gears, flurry, boxed, cubestorm, hyperspace, etc.), most written against classic fixed-function OpenGL 1.x (`glBegin`/`glEnd`, `glMatrixMode`, `glRotatef`, `glOrtho`, GLU) plus a small windowing shim xscreensaver itself provides (`screenhack.h`/`xlockmore.h` — an abstraction over "give me a GL context and an event loop" that already decouples the hacks from X11/GLX details at the call-site level). We want to port the WHOLE CATALOG mechanically, not hand-port hacks one at a time.

The two pieces needed, in order:

### Part 1 — a GL1-on-GLES2 translation shim
Most hacks use fixed-function GL (immediate mode, matrix stack, lighting) which GLES2 does not support natively (GLES2 is shader-only). The established, working solution to this exact problem (running fixed-function GL apps on GLES-only embedded/mobile GPUs) is **GL4ES** (https://github.com/ptitSeb/gl4es) — a library that implements the legacy `gl*`/`glu*` API surface by translating calls into GLES1/GLES2/GLES3 draw calls + an internal shader-based fixed-function-pipeline emulation, at LINK time (apps call the real `glBegin`/`glVertex3f`/etc. symbols, which resolve into gl4es's implementation instead of a real desktop libGL).

Your task: get GL4ES built and working on this host (Debian forky arm64, GLES 3.2 available via Mesa panfrost — GL4ES needs to be told to target GLES2 or GLES3 backend). Confirm feasibility with the smallest possible test: a tiny C program that does classic `glBegin(GL_TRIANGLES); glVertex3f(...); glEnd();` immediate-mode drawing, linked against GL4ES instead of a real GL, rendering into OUR existing wl-screenhack EGL/Wayland surface (reuse the EGL context/surface setup already in wl-screenhack.c — GL4ES sits BETWEEN the app's gl* calls and the real GLES2 driver, it does not need its own EGL/windowing, it just needs an EGL context already made current, which we already have working).

Report exactly how you got it building (repo state, build flags, any patches needed for arm64/Mesa), and whether the fixed-function triangle actually rendered on real hardware (you'll need the coordinator to run this on real hardware — describe EXACTLY what diagnostic output would prove it worked, same style as prior rounds' hardware verification).

### Part 2 — port ONE real, largely-unmodified xscreensaver hack through the pipeline
Once GL4ES is proven working, fetch ONE simple, well-known xscreensaver GL hack's actual source (xscreensaver is GPL2, source is publicly available — e.g. from the xscreensaver project's own repository/tarball; pick something SIMPLE and well-contained, like `glmatrix.c` or a similarly small single-file hack, NOT something with many dependencies). Identify exactly what `screenhack.h`/`xlockmore.h` calls it makes for windowing/lifecycle (window creation, the "give me a GL context" call, get-frame-time, should-I-exit check) and write the MINIMAL compatibility shim in THIS repo (a new file, e.g. `src/xscreensaver_compat.c`/`.h`) that implements just those calls, backed by wl-screenhack.c's existing EGL/Wayland/layer-shell machinery. Get the REAL hack source file compiling against this shim + GL4ES with as few edits to the hack's own source as possible (document every edit you DID need to make and why — the fewer, the better the mechanism scales to the other ~199 hacks).

### Part 3 — document the mechanism
Write/update `PORTING.md`: the general recipe for porting ANY xscreensaver GL hack through this pipeline (fetch source → what shim calls it needs → build against gl4es + xscreensaver_compat → register in the effect table → test). This is the actual deliverable the mechanism needs to scale to all ~200 hacks later, even though this round only proves it on one.

## Constraints
- Reuse the existing wl-screenhack.c EGL/Wayland foundation (rounds 1-6, hardware-verified working) — don't rebuild windowing from scratch.
- Licensing: xscreensaver hacks are GPL2. Vendoring hack source files into this repo is fine (document provenance/license per file) — this is an internal NCZ-OS component, not something with a conflicting license requirement.
- Build cleanliness where it's YOUR code (the compat shim): -Wall -Wextra clean. GL4ES itself and vendored hack source are third-party and don't need to meet that bar.
- If GL4ES turns out to be genuinely broken/unworkable on this hardware after a real attempt (not a guess — you tried, coordinator ran it, it failed with specific errors), say so clearly with the evidence, and propose the next-best mechanical strategy (e.g., a smaller custom fixed-function-emulation shim covering only the subset of GL1 calls xscreensaver hacks actually use, which is a much smaller surface than full GL4ES).

This is a bigger, more open-ended round than 1-6. Take the space you need; report clearly what's proven vs. still uncertain.
