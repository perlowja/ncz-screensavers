# Adopt upstream jwxyz/jwzgles as the compatibility layer — design

**Date:** 2026-09-25
**Status:** approved in principle by the operator ("Let's fix it otherwise
it's going to be hard to maintain"); phased, validated cutover.

## Problem

`ncz-screensavers` hand-rolled its own XScreenSaver compatibility layer.
Measured sizes in our tree today:

| Ours | Lines | Purpose |
|---|---|---|
| `src/gles3_compat.c` | 2,545 | fakes 55 distinct GL 1.x fixed-function calls on GLES3 |
| `src/xscreensaver_compat.c` | 2,180 | Xlib / screenhack-side shim |
| `src/gles3_harness.c` | 756 | Wayland layer-shell windowing + event loop |
| `src/glmatrix_harness.c` | 750 | second harness variant |

Upstream already ships exactly these layers, maintained by the hacks'
own author:

| Upstream | Lines | Purpose |
|---|---|---|
| `jwxyz/jwzgles.c` | ~3–4k | OpenGL 1.3 → OpenGL ES compatibility shim |
| `jwxyz/jwxyz-common.c` | 1,862 | backend-independent Xlib implementation |
| `jwxyz/jwxyz-gl.c` | 2,173 | Xlib-on-GL/GLES backend (has an explicit `HAVE_GLES3` path) |
| `jwxyz/jwxyz.h`, `jwxyzI.h`, `jwxyz-image.c`, `jwxyz-timers.c` | ~1.5k | API surface + support |

So we maintain ~4,700 lines duplicating ~7,900 upstream lines, less
completely and with demonstrated defects.

### This is not theoretical — every colour bug root-caused on 2026-09-25 was in our shim

- **squirtorus** (commit `711010c` root cause): *"star display list captured
  the preceding black mountain material; the compatibility shader also
  retained lighting modulation for the unlit overlay."* Two more instances
  of the same bug (ejecta torus rings and the ground plane rendering pure
  black) were still present afterwards and needed a second fix (`4d77da7`).
- **razzledazzle** (`9501514` root cause): *"unwanted lighting modulation
  reduced rejected facets to the 20% ambient floor because ship setup left
  an arbitrary normal current."*
- `src/xscreensaver_compat.c` fails to build the `_demo` targets on gcc-14
  with an implicit declaration of `glGenerateMipmap`.

These are display-list state capture and fixed-function lighting-state
bugs — precisely the hard, fiddly parts `jwzgles` already implements
correctly. **21 hacks currently fail on both GPU vendors**; it is likely a
meaningful share are the same class of defect.

### Framework sprawl

We currently run five parallel paths: our `gles3_harness` + compat shim,
the gl4es translation path (being retired per the 2026-08-20 directive),
the hyprsaver shader path, the rss-sdl2-gles2 path, and now `xshadertoy`
(landed `e0c890d`). Upstream covers everything with three.

### Catalogue leverage

Measured against upstream 6.16: **140 GL hacks (we build ~92), 144 2D X11
hacks (we build 0), 38 GLSL shaders (ported tonight).** The 2D hacks —
the entire 1990s catalogue — are reachable only through jwxyz, because
they draw with Xlib primitives (`XFillRectangle` ×289, `XDrawLine` ×234,
`XCopyArea` ×131, `XDrawPoint` ×121, `XFillArc` ×113, `XFillPolygon` ×74,
`XDrawString` ×46, `XDrawArc` ×42 across the corpus).

## Design

**Adopt upstream's drawing/compat layers. Keep our Wayland layer.**

```
        hacks (GL)      hacks (2D X11)      hacks (GLSL)
             |                 |                  |
        jwzgles.c      jwxyz-common.c +      xshadertoy.c
      (GL1.3->GLES)      jwxyz-gl.c          (already ours)
             |                 |                  |
             +--------+--------+------------------+
                      |
             src/gles3_harness.c   <-- OURS, KEPT
           (Wayland layer-shell, EGL context, event loop)
                      |
                   GLES3 / EGL
```

- **Replace** `src/gles3_compat.c` with upstream `jwzgles.c`.
- **Replace** `src/xscreensaver_compat.c` with upstream `jwxyz-common.c` +
  `jwxyz-gl.c` (+ headers, `jwxyz-image.c`, `jwxyz-timers.c`).
- **Keep** `src/gles3_harness.c`. Upstream has X11, Cocoa and Android
  backends but **no Wayland backend** — our layer-shell/EGL windowing is
  genuinely novel and is the piece worth owning. jwxyz draws into the
  context our harness creates.
- **Keep** `xshadertoy` as ported; it is already upstream's own design.
- **Retire** the gl4es path once the above lands (already default-off, and
  the 2026-08-20 directive calls for moving off it).

Licence is compatible: jwz's permissive "use, copy, modify, distribute and
sell … without fee" notice. Preserve every header verbatim and record
provenance in `PORTED.md`, matching the `vendor/blackhole-PORTED.md`
pattern.

## Phasing — each phase validated before the next

Hard requirement throughout: **the existing 141 built targets must not
regress.** We have a fresh cross-vendor baseline taken tonight (Intel UHD
and NVIDIA RTX 2060 full-matrix sweeps) and a validation gate that was
repaired tonight (`672389f`, `74a8e3b`, `fc5f339`) to fail on fatal stderr,
use real framebuffer evidence instead of screen-diff, and report
INCONCLUSIVE separately.

1. **Vendor and compile.** Bring `jwxyz/` into `vendor/xscreensaver/jwxyz/`,
   get it building in our meson tree against GLES3. No hack cut over yet.
   Nothing in the existing build changes.
2. **Bridge.** Provide what jwxyz expects (window handle, GL context, event
   loop, timers) from `gles3_harness.c`. Prove it with one trivial hack.
3. **Pilot cutover.** Move a small, deliberately mixed set — including
   `squirtorus` and `razzledazzle`, whose known-good post-fix appearance is
   documented with exact pixel values, plus 2–3 currently-broken ones from
   the 21 — and compare against the baseline on both vendors.
4. **Full cutover.** Move the rest. Re-run the full matrix on Intel and
   NVIDIA; diff against tonight's baseline. **Any hack that regresses
   blocks the phase.**
5. **Retire** `gles3_compat.c`, `xscreensaver_compat.c` and the gl4es path.
6. **Then** the 46 remaining GL hacks and the 144 2D hacks become largely
   mechanical.

## Risks

- **Foundation swap on a mostly-working system.** Mitigated by phasing and
  by the fact that a real baseline + repaired gate now exist.
- **jwzgles targets GLES 1.1/2.0 in places**; we target GLES3.
  `jwxyz-gl.c` has an explicit `HAVE_GLES3` path, but this needs real
  verification in phase 1, not assumption.
- **Our harness is Wayland/layer-shell**; upstream assumes X11/Cocoa/
  Android. The bridge in phase 2 is the genuinely novel work and the most
  likely place to get stuck.
- **Regression churn** on the ~120 hacks that currently work. The phase-4
  gate exists specifically for this.

## Success criteria

- No regression against tonight's cross-vendor baseline.
- A measurable reduction in the 21 both-vendor failures, ideally without
  per-hack fixes — that is the core bet of this design.
- One compat layer instead of five, tracking upstream.

## Does this drag X11 in? No — verified, not assumed

The concern was raised that adopting jwxyz means taking on Xlib. It does
not. Verified directly against upstream `b99f621`:

- **No X11 headers.** `grep '#include <X11'` across `jwxyz-common.c`,
  `jwxyz-gl.c`, `jwzgles.c`, `jwxyz.h` and `jwxyzI.h` returns nothing.
- **No X11 linkage.** No `-lX11` / `libX11` in `jwxyz/Makefile.in`.
- `jwxyz.h` opens with the line: `/* JWXYZ Is Not Xlib.`

jwxyz *reimplements the shape of the Xlib API* on top of GL/GLES. That is
the entire reason it exists: jwz wrote it so the hacks could run on macOS,
iOS and Android, none of which have X11. Adopting it requires no X server,
no Xwayland and no libX11. What it does bring is Xlib-*shaped* names
(`XFillRectangle`, `Display*`, `GC`) in vendored code.

`jwzgles` and `jwxyz` are independent and separable:
- `jwzgles` alone (GL 1.3 → GLES) fixes the fixed-function/display-list/
  lighting bug class behind our broken hacks and makes the 46 remaining GL
  hacks mechanical. No Xlib-shaped API at all.
- `jwxyz` is required only for the 144 2D hacks.

**Decision (operator, 2026-09-25): adopt both.**

### Repo boundary — whose review rules apply

`ncz-screensavers` is NCZ-owned (`perlowja/ncz-screensavers`,
`gitlab.com/ncz-os/ncz-screensavers`). The Singularity upstream
contribution rules in `~/.claude/rules/singularity-contribution-guidelines.md`
govern `singularityos-lab/*` — singularity-shell and singularity-desktop —
not this repo. Vendored compat code here is not subject to that review.

The piece that IS subject to it is the screensaver **settings UI**
(`docs/superpowers/specs/2026-09-25-screensaver-preferences-design.md`,
fork branch `feat/screensaver-preferences` on
`perlowja/singularity-shell`). That design already carries the required
constraints: libsingularity widgets only (no libadwaita), no NCZ-specific
names or paths in the generic layer, and the per-hack schema data kept
downstream. Nothing from jwxyz belongs in that UI.
