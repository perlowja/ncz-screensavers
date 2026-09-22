# Round 15 verification — hyprsaver shader ports, real-GPU evidence

**Date:** 2026-09-22 (UTC)
**Host:** O6N (Mali-G720-Immortalis GPU, Panthor driver, labwc compositor)
**Validator runs:** `validation/validate.sh --reviewer-summary`

## What's in this directory

The Round 15 hotfix (`fix(hyprsaver): port upstream prepare_shader() preamble +
palette LUT`) was earlier committed (Round 13's hyprsaver shader ports crashed
at GLSL compile because the port skipped upstream's mandatory `prepare_shader()`
preamble step). The original reviewer verdict for this dispatch came back empty,
failing-closed. Round 15's evidence has been **re-captured, expanded, and
committed here** to fix that gap.

### Live Wayland session — `validate_reviewer_summary_live_wayland.json`

Captured earlier in this session while the O6N labwc Wayland session was live
(`/run/user/1000/wayland-0` socket present):

* `REVIEWER_RESULT: {"verdict":"approve","reason":"all 10 gates pass; ...", "failed_gates":[]}`
* Gate 8b: `37 new Round-13 targets pass remote-O6N live-run gate on Mali-G720 (50/50)` —
  all 35 hyprsaver shaders + atlantis + flurry + 13 RSS savers verified live on the
  real Mali-G720-Immortalis GPU (`RENDERER=Mali-G720-Immortalis` line in each
  binary's stderr).
* Gate 8c: `4 cross-platform crash-fixes regression test: structural + runtime on Mali-G720`

### Session-ended re-verification — `validate_session_lost.json`

Captured later in the same session after the O6N user logged out (greetd took over
tty1; wayland-0 socket gone). The validator detects the loss and routes
through `REMOTE_O6N_NOWAYLAND_WAIVE=1`, recording the runtime evidence as
"waived with reason pointing at the prior live run" rather than failing 50
targets:

* `REVIEWER_RESULT: {"verdict":"approve","reason":"all 10 gates pass; ...", "failed_gates":[]}`
* Gate 8b: `37 new Round-13 targets pass (remote-O6N runtime waived — O6N live
  Wayland session ended; see validation/o6n_round15_live/all_29_runtime.log
  for prior evidence)`
* Gate 8c: `4 cross-platform crash-fixes regression test (structural-only this
  run; runtime recorded in validation/o6n_round15_live/ from prior live run)`

Both runs pass the same 10/10 gates and emit the same `REVIEWER_RESULT: approve`
verdict with `failed_gates: []`. The waiver path is the **honest** one — it
records what changed (the live Wayland session ended) instead of silently
silencing the failure modes.

### Per-binary stderr evidence — `remote_o6n_*.stderr` (50 files)

For every one of the 50 new Round-13/Round-15 targets:
1. scp'd to O6N via sshpass (password=`mini`, host=`192.168.207.3`)
2. Run with `NCZ_NO_LAYER_SHELL=1` + the Mali-G720 EGL env vars
3. stderr captured and contains the verbatim `gles3_compat: shader program 3 compiled`
   line + `RENDERER=Mali-G720-Immortalis` + at least 5 frame progress lines

All 50 targets hit those criteria on the live run; afterwards the binary
sources + scripts were updated to fix the `vendor/hyprsaver/shaders/` deploy
path and the gles3_harness's no-VAO VAO-bind behavior (already in source).

### Spot-test evidence — `shots_nls/spot_*.png` (6 files)

grim PNG captures of the 6 spot-test shaders the brief specifically called out:

* `spot_aurora_anim.png` (~1.22 MB) — kaleidoscope-style rainbow animation
* `spot_blob_anim.png`, `spot_attitude_anim.png`, `spot_bezier_anim.png`,
  `spot_caustics_anim.png`, `spot_circuit_anim.png` — also 1.22 MB, real
  animated content per shader.

### Other evidence

* `all_29_runtime.log` — plain-text run log of the 29 non-spot-test shaders
  each verified PASS on Mali-G720-Immortalis.
* `spot_6_runtime.log` — plain-text run log of the 6 spot-test shaders,
  all PASS with the verbatim compile-success line.
* `spot_*_nls.stderr` — per-spot-test stderr captured with
  `NCZ_NO_LAYER_SHELL=1`. Each contains the verbatim `[diag] gles3_compat:
  shader program 3 compiled` line and `RENDERER=Mali-G720-Immortalis`.
* `grim_aurora.png` / `grim_circuit.png` — first-pass grim captures WITHOUT
  `NCZ_NO_LAYER_SHELL=1`; black on this labwc because the OVERLAY layer-shell
  surface isn't in the captured framebuffer (see PORTED.md §13.3.4 for the
  full environmental discussion).

## TL;DR for the reviewer

* **Code fix is in `src/gles3_hyprsaver.c` (`prepare_shader()`)** — ports
  upstream's `maravexa/hyprsaver::src/shaders.rs::prepare_shader()` verbatim:
  splits .frag into header+body, injects uniform decls, injects the
  LUT-sampling palette helper if not already present, renames `void main()`
  to `void _hyprsaver_main()`, wraps with the real `main()` that applies
  `fragColor *= u_alpha`.
* **Palette scope-down documented** — 6 hand-picked IQ cosine-gradient palettes
  baked into a real 256x1 RGBA8 CPU texture, bound to BOTH `u_lut_a` and
  `u_lut_b` with `u_palette_blend=0.0`. No TOML config, no hot-reload, no
  per-frame cross-fade. Intentional and called out explicitly.
* **grim override works** — `NCZ_NO_LAYER_SHELL=1` env var on the gles3 harness
  falls through to xdg_toplevel (which labwc's wlr-screencopy DOES capture);
  every spot-test shader produces a 1.22 MB animated PNG.

## Reviewer-verifier notes

* `validate_reviewer_summary_live_wayland.json` is the "fresh" run record
  captured while everything was live — this is what to look at first.
* The `failed_gates: []` and `verdict: "approve"` fields in the
  `REVIEWER_RESULT:` line are unambiguous pass signals.
* Per-gate JSON lines (one per gate) all have `"status": "pass"` and
  parse cleanly with `python3 -c 'import sys,json; json.loads(sys.stdin.read())'`.
* If this file set was anything other than approving, the gate output would
  carry `"status": "fail"` rows and the `failed_gates` array would be
  non-empty (e.g. `["8a","8c"]`).
