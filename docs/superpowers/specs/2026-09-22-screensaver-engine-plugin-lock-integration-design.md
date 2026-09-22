# Screensaver engine + chooser plugin + loginui/lockscreen unification proposal

**Date:** 2026-09-22
**Status:** draft for review

## Goal

Ship a screensaver chooser UX for NCZ-OS/Singularity Desktop: Random or
choose-specific selection from the 90 GLES3-native `ncz-screensavers` hacks,
per-hack configurable parameters where a hack exposes any, an idle timeout,
and lock-screen integration. Package this as three separable deliverables so
each can be reviewed and merged (or rejected) on its own terms, with the
riskiest one (loginui/lockscreen unification) proposed to Mirko as a design
discussion rather than landed unilaterally.

## Current state (verified this session)

- `ncz-screensavers`: 90 of 94 hacks are GLES3-native (`<name>_gles3`
  binaries). `include/wlscreensaver.h` specifies an embeddable engine API
  (`wlss_create`/`wlss_resize`/`wlss_frame`/`wlss_destroy`, config struct with
  `effect` name + `options` as `"key=value"` strings) — **this is a header
  only, nothing implements it yet**, and it is not wired into
  `meson.build`. `src/gles3_harness.c` is the reference pattern for
  Wayland+EGL setup and the frame loop (just fixed a real animation-stall bug
  there today, commit `c6f41fb`), but it OWNS its own surface/shell — the
  engine has to do the same GL/EGL work while being HANDED a surface instead.
- `src/xscreensaver_compat.h`/`.c` already has `get_string_resource()` /
  `get_boolean_resource()` / `get_integer_resource()` / `get_float_resource()`
  — the per-hack parameter mechanism most upstream xscreensaver hacks already
  use, currently backed by `getenv()` as a stand-in for X resources. This
  maps directly onto `wlss_config.options` (`"density=60"`, `"fog=false"`,
  etc.) — the plumbing already exists, it just needs the engine to translate
  `options[]` into environment/resource lookups before calling a hack's
  `init_cb`.
- Singularity's plugin system (`singularity-plugins/`): `libpeas`-based Vala
  plugins, `.plugin` metadata + `meson.build` installing to
  `libdir/singularity/plugins/<name>/`, implementing `Singularity.Plugin`
  (`activate(ctx)` / `deactivate()` / `get_settings_widget()`). A
  `ShellSurfaceProvider`/`ShellRole` mechanism exists for replaceable shell
  chrome (dock/panel/launcher/etc.) but has **no screensaver or lock role** —
  idle-screensaver and lock don't fit that contract and shouldn't be forced
  into it.
- `singularity-loginui`: a shared, toolkit-less Cairo render library
  explicitly built to keep the greeter, lock screen, session splash and boot
  splash pixel-identical across different backends (Wayland layer-shell or
  raw KMS). This is why the lock screen already "looks similar to the
  greeter" — rendering is already shared.
- What's NOT shared: application logic. `singularity-desktop`'s subprojects
  `singularity-greeter` (`greeter_main.c`, launched by greetd, pre-session,
  no Wayland session exists yet) and `singularity-shell/src/lockscreen`
  (`lock_main.c`, `lock_screen.vala`, `lock_surface.c/h`, in-session,
  `ext-session-lock-v1`) are separate binaries with separate app logic, PAM
  handling, and event loops. `wlscreensaver.h`'s own header explains why a
  screensaver-during-lock has to be embedded INSIDE whichever process owns
  the lock surface: `ext-session-lock-v1` permits exactly one lock client per
  session, and Wayland subsurfaces must come from the same `wl_client`, so a
  separate screensaver process cannot attach a surface beneath the lock
  prompt.

## Scope decomposition — three deliverables, three PRs

### 1. `libwlscreensaver` — implement the engine (ncz-screensavers)

Implement `wlscreensaver.h`'s contract as a real shared library:

- `wlss_create()`: given a `wl_display`+`wl_surface` the HOST already owns
  (not created by the engine), do the EGL setup `gles3_harness.c` already
  does (minus all the Wayland surface/shell/xdg/layer-shell code — the host
  did that part), bind to the host's surface via `eglCreateWindowSurface`
  against a `wl_egl_window` wrapping the host's `wl_surface`.
- `wlss_effect_names()`: the compiled-in hack name table (reuse the existing
  `ported_hacks`/`legacy_gles3_hacks` meson tables as the source of truth,
  generate the C table from the same list so it can't drift).
- `wlss_frame()`: call the selected hack's `draw_cb`, do NOT call
  `eglSwapBuffers` itself if the host wants to control presentation timing —
  check whether the header's contract implies the engine swaps or the host
  does (currently ambiguous in the header as written; resolve this as part of
  implementation, likely the engine swaps since it owns the EGL surface, and
  document it explicitly if the header is unclear).
- `options[]` parsing: before calling a hack's `init_cb`, translate each
  `"key=value"` into what `get_string_resource()`/etc. already read
  (currently `getenv()`) — either extend the compat shim to also check an
  in-process option table the engine populates, or `setenv()` per-process
  (simpler, but not thread-safe / not safe for multiple concurrent engine
  instances in one process — note this constraint either way).
- Failure containment: the header's own security requirement (a crashed
  effect must not unlock or blank an active lock prompt) — implement
  `wlss_frame()` to run the hack's draw call in a way the host can bound in
  time and treat failure as effect-only, per the header's own documented
  contract.
- New `meson.build` target: `shared_library('wlscreensaver', ...)`,
  installable, with a pkg-config file so `singularity-plugins/screensaver`
  and the lockscreen/greeter binaries can `dependency('wlscreensaver')`.

This PR stays entirely inside `ncz-screensavers` — no dependency on the
other two deliverables, reviewable and mergeable standalone.

### 2. `singularity-plugins/screensaver` — the chooser plugin

A `libpeas`/Vala plugin, same shape as `example-dock`, providing
`get_settings_widget()` only (no `ShellSurfaceProvider` — this isn't
replaceable shell chrome):

- Random vs. choose-specific: a `GSettings` enum/string key
  (`screensaver-mode`: `"random"` | `"specific"`) + a list key for which
  specific effect when in `"specific"` mode.
- Per-effect params: expose whatever `options` a given hack actually reads
  via `get_*_resource()` — needs a small per-hack metadata source (most
  xscreensaver hacks document their own `-resources`-style options; a first
  pass can hardcode a handful of the more commonly-tweaked ones — e.g.
  density/speed/fog on the hacks that already reference those resource
  names — and leave the rest at defaults rather than inventing schema for
  90 hacks up front. YAGNI: ship what's real, extend later.)
- Idle timeout: a `GSettings` integer key (minutes), read by whatever
  currently owns idle-timeout policy in the shell (need to confirm this
  during implementation — don't invent a new idle-watcher if
  `singularity-shell` already has one for existing power/lock policy).
- This plugin does NOT itself launch the screensaver or own a lock surface —
  it only manages the settings. The actual "launch effect on idle" /
  "launch effect on lock" logic is deliverable 3's job, since that's where
  the surface ownership constraint lives.

This PR depends on deliverable 1 (links `libwlscreensaver`) but not on
deliverable 3 — the settings UI can exist and be reviewed even before the
lock-screen integration lands, since `get_settings_widget()` only writes
GSettings keys.

### 3. Proposal to Mirko: loginui/lockscreen unification (design discussion, not a code PR)

Open as a GitHub discussion/issue on `singularityos-lab/singularity-shell`
(not a PR — this changes how his core session/lock/greeter components relate
to each other, exactly the kind of structural decision that needs his
buy-in before code, per the existing contribution rulebook's pattern of
discussing scope before large changes). Present all three approaches
considered, recommend one:

- **Option A — single binary, mode-switched at launch.** Most literally
  "the same": one codebase for both greeter and lock screen, choosing
  behavior from how it's invoked (greetd's pre-session IPC launch vs.
  `ext-session-lock-v1`'s in-session client protocol). Highest risk: these
  are genuinely different lifecycles (no Wayland session exists yet for the
  greeter; the lock screen runs inside an already-running session's `labwc`)
  and collapsing them risks fighting both protocols at once.
- **Option B (recommended) — shared app-core library.** Factor out a
  library (auth-flow, input handling, and a new
  screensaver-embedding hook using deliverable 1's `libwlscreensaver`) that
  both `greeter_main.c` and `singularity-shell/src/lockscreen/lock_main.c`
  link against — the same pattern `singularity-loginui` already established
  for rendering, extended to application logic. Same features and behavior
  in both without forcing two genuinely different launch protocols into one
  process.
- **Option C — screensaver-only, no unification.** Add the
  `libwlscreensaver` embedding hook to `lock_main.c` alone, defer
  greeter/lock convergence entirely. Ships deliverable 2's chooser UX fastest
  but doesn't address the "greeter and lock screen should be the same" goal
  at all.

Recommend B, with the reasoning above, and note that decision gates whether
deliverable 2's "launch on idle/lock" logic lands in a new shared library
(if B ships) or directly in `lock_main.c` alone (if C is preferred instead,
or as an interim step while B is designed).

## Sequencing

1→2→(3 in parallel, since it's a proposal doc, not blocked on code). 3's
OUTCOME determines where deliverable 2's actual "launch effect" wiring goes,
so that wiring is deliberately left out of deliverable 2's PR — it ships the
settings UI only, and a follow-up PR wires the launch once Mirko responds to
3.

## Open questions to resolve during implementation, not before

- Does `wlss_frame()` swap buffers itself, or does the host? (header is
  ambiguous — resolve as part of deliverable 1, document explicitly.)
- Which existing idle-watch mechanism (if any) already exists in
  `singularity-shell` for power/lock policy — reuse it, don't duplicate.
- Exact per-hack options to expose in v1 of the settings UI — start small
  (a handful of hacks with well-known resource names), not all 90 at once.

## Validation

- Deliverable 1: a small test host (reuse `gles3_harness.c`'s Wayland setup
  as a throwaway test harness) that creates a surface, hands it to
  `libwlscreensaver`, and confirms real animated frames render — same
  live-verification discipline as today's harness fix (`fuser`/frame-count
  proof, not just "it compiled").
- Deliverable 2: `get_settings_widget()` round-trips GSettings keys, checked
  interactively (per this project's own contribution norms — a build that
  compiles is not evidence a feature works).
- Deliverable 3: no code to validate; success is Mirko's response.

## Attribution / process (per singularity-contribution-guidelines.md)

All three land on `perlowja`'s fork, `Assisted-by`/`AI-Scope` trailers per
the repo's CI-enforced format, near-zero inline comments (Mirko's repeated
ask), one concern per PR (deliverables 1 and 2 must not be bundled into one
PR even though they're related).
