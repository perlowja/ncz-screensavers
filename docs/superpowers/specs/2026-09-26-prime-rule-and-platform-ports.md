
# When to use PRIME offload — measured rule

Measured on PEGASUS 2026-09-26 (Intel UHD CML GT2 + RTX 2060, muxless),
frames rendered in 10 s at 1920x1080.

| shader | iGPU | offloaded | outcome |
|---|---|---|---|
| `cellmosaic` | **600** (vsync-capped 60fps) | 540 | offload **loses 10%** |
| `hexlattice` | **600** (vsync-capped) | 540 | offload **loses 10%** |
| `wellcurve` | 540 | 540 | no gain |
| `blackhole` | 120 (12fps) | **540** | offload wins **4.5x** |
| `ridgeline` | 4 (0.4fps) | **300** | offload wins **75x** |

## The rule

**Offload only when the iGPU cannot hit the frame-rate target.**

The mechanism: on a muxless laptop the iGPU owns the panel, so an offloaded
frame must be copied BACK across the bus for scanout. That copy is a fixed
per-frame cost. A shader already hitting vsync on the iGPU gains nothing and
pays the copy - measured at roughly 10%. A GPU-bound shader makes the copy
irrelevant against a 4.5x or 75x render gain.

## Decision procedure for the adaptive tier

1. iGPU >= target (e.g. 55fps at 60Hz) -> **stay on the iGPU, do not offload**
2. iGPU < target -> offload, then re-measure
3. still < target after offload -> reduce internal render scale via
   `wp_viewporter`; do not drop frames
4. on battery -> prefer the iGPU even at reduced quality. A screensaver is an
   idle workload and waking a discrete GPU for it costs real watts

## Two refinements

- The copy cost scales with resolution, so offload is relatively more
  expensive at 4K than at 1080p. Measure the threshold per resolution rather
  than assuming one.
- Cache the probe per `(shader, resolution, GPU)`. Re-probing on every
  screensaver activation would impose a measurement cost on every idle event.

**Never set the offload variables ambiently** - see
`trap-ambient-prime-offload-breaks-compositor`. Per-launch only.

---

# Windows and macOS ports — measured starting position

## The debt is small

Per `2026-09-25-platform-abstraction-rule.md`, measured: **194 of 206 source
files are platform-agnostic.** Only 12 reference Wayland/EGL, most of them the
frozen legacy path. The genuine platform surface is three files -
`gles3_harness.c` (756 lines), `glmatrix_harness.c`, `wl-screenhack.c`.

The entire POSIX debt across the modern shader hacks is **two calls**:

| call | Windows substitute |
|---|---|
| `clock_gettime(CLOCK_MONOTONIC)` | `QueryPerformanceCounter` |
| `/dev/urandom` via `open`/`read` + `getpid` | `BCryptGenRandom` |

macOS supports both natively. (Note: the `iSeed` work added on 2026-09-26 uses
exactly these, so it sits inside the known debt rather than adding new.)

This is a SHELL problem, not a rewrite. The shader core already ports.

## Toolchain — gcc where it is real, clang where it is not

| target | toolchain | status |
|---|---|---|
| Linux | gcc | in use |
| **Windows** | **MinGW-w64 gcc** | `gcc-mingw-w64` available in apt on HYDRA/ACHILLES (13.2) and PEGASUS (16.2), **not yet installed**. Cross-compiles from Linux |
| **macOS** | core: gcc or clang; **shell: clang** | STUDIO is Darwin arm64 with clang/gcc/make. The `.saver` bundle is a `ScreenSaverView` subclass in Objective-C against Apple frameworks - realistically clang plus the Xcode SDK, not gcc |

So "gcc across all platforms" holds for Linux and Windows and for the macOS
*core*, but not for the macOS *shell*. Cross-compiling macOS from Linux would
need osxcross plus an Apple SDK, which carries its own licensing question;
building natively on STUDIO avoids that entirely.

## Per-platform shell work

| platform | window/surface | GL | packaging |
|---|---|---|---|
| Linux | `zwlr_layer_shell_v1` + EGL | GLES3 native | existing |
| Windows | Win32 window | **ANGLE** (GLES3 over D3D11) | `.scr` (an .exe honouring `/s /p /c`) |
| macOS | Cocoa `ScreenSaverView` | **ANGLE** (GLES3 over Metal) - macOS has no GLES3 and its OpenGL is deprecated at 4.1 | `.saver` bundle |

ANGLE on both non-Linux targets keeps a single GLES3 shader corpus, which is
the whole point of having gone shader-first.
