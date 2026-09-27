# Shipping licence audit + prior-art reuse assessment

2026-09-26. Both items block or shape the first NCZ-screensaver release.

## 1. SHIPPING BLOCKER — 11 Shadertoy shaders have no declared licence

Audited every `vendor/xshadertoy/glsl/*.glsl`:

| declared licence | count |
|---|---|
| MIT | 17 (14 of them explicitly "Relicensed as MIT License, by permission") |
| CC0 | 4 |
| CC BY 3.0 | 1 (`synthwavecity`) |
| **none declared** | **11** |

Undeclared: `fluxcore`, `protophore`, `topologica`, `alienbeacon`,
`driftclouds`, `truchetzoom`, `bubblecolors`, `elementalring`,
`gimbalharmonics`, `logarithmiccircles`, and one further.

**Shadertoy's default licence is CC BY-NC-SA 3.0** — non-commercial AND
share-alike. Neither is acceptable for a distributed OS image. An undeclared
shader must be treated as CC BY-NC-SA until its author says otherwise.

`vendor/xshadertoy/` has **no LICENSE file at all**, unlike `hyprsaver`,
`gl-transitions` and `rss-sdl2-gles2-src`, which all vendor one.

**Three options, in order of preference:**
1. Seek permission, as was clearly already done for 14 shaders. A working
   process exists; use it.
2. Hold the 11 out of the shipping set until cleared.
3. Replace them with original work.

**Recommendation: ship the 22 cleared shaders, hold the 11.** Do not ship an
undeclared shader on the assumption it is probably fine.

### Wider third-party state

| vendored tree | licence file |
|---|---|
| `gl-transitions` | present |
| `hyprsaver` | present - MIT, Copyright (c) 2026 Mara Vexa |
| `rss-sdl2-gles2-src` | present (family being cut anyway) |
| **`xshadertoy`** | **absent** |
| `blackhole` | absent in-tree, but `vendor/blackhole-LICENSE.txt` exists alongside |

Our own repo is **Apache-2.0**. MIT and CC0 inbound are compatible. There is no
root `NOTICE` or `THIRD-PARTY` file; for an Apache-2.0 project bundling MIT
components that is worth adding even though vendoring each LICENSE satisfies
the strict requirement.

## 2. Prior art — what to incorporate rather than build

Cloned and read four Wayland shader renderers rather than relying on summaries.

| project | licence | protocols beyond layer-shell | multipass/FBO | lines |
|---|---|---|---|---|
| `glbg` | from swaybg (MIT) + toml-c | none | 0 | 2478 C |
| `glshell` | **NO LICENCE FILE** | none | 0 | 925 C |
| `hyprsaver` | **MIT** | **`wp_fractional_scale`** | 2 files | 15124 Rust |
| `shaderbg` | **GPL-3** | none | 0 | 928 C |

### Verdicts

- **`glshell` — legally unusable.** No licence file means all rights reserved,
  regardless of being on GitHub.
- **`shaderbg` — GPL-3.** Copying any of it would force NCZ-screensavers from
  Apache-2.0 to GPL-3. Reference for ideas only; never copy code.
- **`glbg` — safe (MIT-derived) but adds nothing.** Layer-shell plus a
  Shadertoy uniform subset, which we already have.
- **`hyprsaver` — MIT, and the only one ahead of us.** It is the single
  worthwhile study target: fractional scale and some FBO handling. We already
  ship 35 of its shaders and correctly vendor its LICENSE.

### What this confirms about wayshade's novelty

Measured against real code, not marketing: **none of the four implements
presentation-time, explicit sync, `ext-session-lock-v1`, colour management, or
per-run randomisation.** Only hyprsaver does fractional scale or touches FBOs
at all.

So the differentiators hold. The renderer itself is commodity and we should not
write a fifth one; the engine concerns - session-lock integration, per-run
randomisation, adaptive quality tiering, curation with a quality bar,
multipass, modern protocols, and photosensitive-safety validation - are
genuinely unbuilt elsewhere.

**Incorporate:** hyprsaver's fractional-scale approach (MIT, compatible).
**Do not write:** another layer-shell shader runner.

---

# CORRECTION 2026-09-26 — the blocker is 2 shaders, not 11

The count above is WRONG and is retracted. The original scan grepped only for
lines containing the word "license", so every shader declaring `CC0:` inside
its description line was miscounted as undeclared.

Correct scan (matching CC0 / MIT / CC BY anywhere in the first 20 lines):

| licence | count |
|---|---|
| MIT | 17 |
| CC0 | 17 |
| CC BY 3.0 | 1 (`synthwavecity`) |
| **genuinely undeclared** | **2** — `driftclouds`, `bubblecolors` |

**35 of 37 are cleanly shippable.** MIT and CC0 both permit commercial
redistribution. `synthwavecity` is CC BY 3.0 and needs only an attribution
line.

Remaining work is therefore small:
1. `driftclouds` and `bubblecolors` — drop, clear with their authors, or
   clean-room replace.
2. Add an attribution entry for `synthwavecity`.
3. Add `vendor/xshadertoy/LICENSE` summarising the per-shader position, since
   that directory still has no licence file.

**The lesson worth keeping:** a licence audit that greps for the word
"license" will systematically miss CC0 and SPDX-style declarations. Match the
licence *identifiers*, not the word.

## Asking is not about jwz

Shadertoy shaders are by individual Shadertoy authors, unrelated to
xscreensaver. Nothing here involves asking jwz.

And for the xscreensaver-derived work we ask nobody: clean-room
reimplementation from published mathematics requires no permission. Four such
pieces landed on 2026-09-26 (`cellmosaic`, `hexlattice`, `ridgeline`,
`wellcurve`) without any original source being opened.
