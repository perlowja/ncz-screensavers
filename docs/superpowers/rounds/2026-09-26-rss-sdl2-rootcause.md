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

1. Identify what is in `common_gles3_sources`.
2. Read `gles3_harness.c` — it is the harness for every one of the 13.
3. Look at one passing and one failing per-hack source for a clue.

## Initial state
- 2026-09-26 09:54 EDT — start.
- Git HEAD: 71c521c, working tree clean.
- Branch master tracking origin/master.

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