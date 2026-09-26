# Platform abstraction — standing architectural rule

**Date:** 2026-09-25. Operator direction: *"Let's keep it platform
abstract so we can put our stuff on those engines as much as possible."*

The goal is that every screensaver we write runs on Linux/Wayland,
Windows and macOS with no per-hack changes. This is cheap to hold now and
expensive to retrofit, so it is a rule from here on, not an aspiration.

## Where we already stand (measured, 2026-09-25)

**12 of 206 source files reference Wayland/EGL at all.** Of those, most
are the frozen legacy path we are not porting (`gles3_compat.c`,
`xscreensaver_compat.c`, `flurry*.c`, `spline.c`, `gl4es_triangle.c`).
The genuine platform surface is:

- `src/gles3_harness.c` (756 lines) — Wayland layer-shell + EGL + event loop
- `src/glmatrix_harness.c`
- `src/wl-screenhack.c`

**No shader hack touches platform code.** blackhole, lavafield,
xshadertoy, hyprsaver, and the in-flight genxvectorcade and vector shooter
are platform-agnostic C plus GLSL. 194 of 206 files do not care what OS
they are on.

That position is an accident of going shader-first rather than adopting
fixed-function GLES 1.1, and it is worth protecting deliberately.

## The actual portability debt today

Measured across the modern shader hacks, it is exactly two things:

| Call | Used by | Windows status |
|---|---|---|
| `clock_gettime(CLOCK_MONOTONIC)` | all shader hacks | not available on MSVC |
| `/dev/urandom` + `open`/`read`/`getpid` (`unistd.h`, `fcntl.h`) | blackhole, lavafield | not available |

macOS supports both natively. Only Windows needs substitutes
(`QueryPerformanceCounter`, `BCryptGenRandom`).

## The rule

**Hack code is pure C plus GLSL ES 3.00. It may not:**

- include platform headers (`wayland-*.h`, `EGL/*`, `windows.h`,
  `unistd.h`, `fcntl.h`, Cocoa/ObjC headers)
- call windowing, context, input or display APIs directly
- read platform paths or devices directly (`/dev/urandom`, `/proc`, ...)
- call POSIX-only time, process or threading functions
- rely on GL beyond the **core GLES 3.0** feature set — no desktop-GL-isms,
  no extensions, no compute, no geometry shaders

**Everything platform-specific goes behind a small C interface** that each
backend implements. Based on what the hacks actually use, that interface
is genuinely small:

```
ncz_now()            monotonic seconds as double
ncz_seed()           entropy for per-launch randomisation
ncz_asset_path()     resolve an installed shader/asset by name
                     (install dir differs per platform)
ncz_frame_size()     current drawable size
ncz_swap()           present the frame
```
plus context/window lifecycle owned entirely by the backend.

Backends: `platform/wayland.c` (refactor of the current harness),
`platform/win32.c`, `platform/macos.m`. Objective-C is a strict superset
of C, so the macOS backend calls our C directly — no bindings, no FFI.

## Consequences to honour

- **Fix the two existing leaks** when the interface lands: replace
  `clock_gettime` with `ncz_now()` and `/dev/urandom` with `ncz_seed()`
  across blackhole, lavafield, xshadertoy and hyprsaver. Small, mechanical,
  and it validates the interface against real callers.
- **Asset paths must not be hardcoded.** `/usr/share/ncz-screensavers/
  shaders/` is a Linux answer; Windows and macOS differ. Shader lookup
  goes through `ncz_asset_path()`. (This already bit us once: a
  cwd-relative shader path killed the flagship blackhole hack when
  launched from any other directory while still passing the validation
  gate.)
- **Prefer ANGLE as the GLES3 provider** for Windows (D3D11) and macOS
  (Metal). It keeps every shader byte-identical and sidesteps Apple's
  OpenGL deprecation. Cheapest to adopt now, at five shader hacks.
- **The frozen legacy shim is explicitly out of scope.** It is not ported
  and its Wayland/EGL entanglement does not constrain this design.
- **New hacks must comply from the first commit.** Any new hack that
  includes a platform header or calls a POSIX-only function is a bug, not
  a port task for later.

## Honest caveat

This is measured from file-level greps, not from reading the harness
internals. `gles3_harness.c` may have Wayland assumptions threaded through
its structure — surface roles, frame callbacks, output enumeration — that
make extracting a clean interface more work than the file count suggests.
Read it properly before committing to an estimate.
