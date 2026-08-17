Round 9: extend xscreensaver_compat with the standard xlockmore options-table declaration machinery. You are on macOS, cannot build; the coordinator builds on Debian forky arm64 and reports exact compiler output back. Round 7's shim + round 8's fixes got glmatrix.c through EVERY prior compile stage (headers, EGL, GLU math, module registration) — this is the next real gap, discovered only by actually compiling on real hardware.

## The exact errors (from a real forky gcc build, `-DUSE_GL` now correctly defined)
```
../src/glmatrix.c:219:8: error: unknown type name 'argtype'
../src/glmatrix.c:220:4: error: initialization of 'int' from 'char **' makes integer from pointer without a cast
../src/glmatrix.c:220:4: error: initializer element is not computable at load time
../src/glmatrix.c:220:57: error: 't_String' undeclared here (not in a function)
../src/glmatrix.c:221:4: error: initialization of 'int' from 'GLfloat *' makes integer from pointer without a cast
../src/glmatrix.c:221:57: error: 't_Float' undeclared here (not in a function)
```
(density/t_Float repeats similarly for more entries in the same table — pattern continues past what's pasted here.)

## What this is (general xlockmore/screenhack knowledge, not specific to any one hack)
xscreensaver's xlockmore-based hacks declare their command-line options via a standard `vars[]` array — one entry per option, each holding: a pointer to the variable it controls, the option name string, the X-resource class-name string, a default-value string, and a TYPE TAG telling the framework how to parse/apply that default (as a string, a float, an int, a boolean, etc.). The type tag is an enum (`argtype` in the framework, with values conventionally named `t_String`, `t_Float`, `t_Int`, `t_Bool` — check `glmatrix.c`'s actual `vars[]` table for the full set of type values it references; there may be others beyond String/Float, e.g. `t_Int`/`t_Bool`, scan the whole array not just the first two rows).

## What to do
1. Read `glmatrix.c`'s actual `vars[]` array declaration (and the `argtype`-typed entries specifically) to find every distinct type-tag value it uses.
2. Add the `argtype` enum + a matching struct type (holding `{void *var; char *name; char *classname; char *def; argtype type;}` or whatever exact shape `vars[]`'s entries require to compile — infer the exact field order/types from the compiler's own initializer-order errors) to `xscreensaver_compat.h`.
3. This table normally gets CONSUMED somewhere (the xlockmore/screenhack framework parses these entries against X resources / argv at startup to override the compiled-in defaults). Since we have no X server, no resource database, and (per round 7's PORTING.md framing) currently support configuration via `NCZ_SCREENHACK_EFFECT` env-var selection only — for THIS round, it is sufficient for the `vars[]` table to just COMPILE correctly (satisfy the type system) with a no-op or stub consumer; do not build a full env-var/argv option-parsing system this round unless it's trivial. Note in PORTING.md if real option overrides (e.g. `-speed`, `-density`) are deliberately deferred to a future round, and why.
4. Keep iterating past this error to whatever comes next in the SAME build (glmatrix.c is a large file; there may be more distinct gaps after this one clears — keep fixing what you can reason about confidently, and clearly document/defer anything you can't verify without a real compile).
5. If you get it fully compiling+linking, say so and give the coordinator the exact run command + expected visual (matching round 7's `ROUND7_VERIFICATION.md` style) so it can be tested on real O6 hardware. If you get partway and hit something you're not confident fixing blind, stop and report the exact remaining error(s) — same discipline as before, don't guess without evidence.

## Design note carried over from operator discussion (fold in if you have time; not blocking)
Two xscreensaver framework mechanisms are relevant to how ported hacks will eventually behave, worth keeping in mind as you build out compat shim coverage (do not need to fully implement this round, just don't design something incompatible with it):
- **Randomization**: xscreensaver's framework seeds one PRNG per-process at startup before a hack's init runs; hacks then call `frand()`/`random()`-style helpers freely. Our shim should do the same (seed once, expose the helper under whatever name the vendored source expects) — check whether xscreensaver_compat.c already does this correctly.
- **Frame pacing**: xscreensaver's classic model is poll-sleep(delay)-redraw with no vsync; our foundation (wl-screenhack.c rounds 1-6) is compositor-driven frame callbacks (vsync-paced), which is better and should NOT be replaced. The eventual design: track elapsed real time in the harness and only actually redraw when it exceeds a hack's requested delay, rather than literally reimplementing the old sleep loop. Not required this round — glmatrix_harness.c's current frame-driven loop is fine for now — just don't paint the shim into a corner that makes this translation impossible later.

Build cleanliness for anything YOU author (xscreensaver_compat.{h,c}): -Wall -Wextra clean, same bar as before. The vendored glmatrix.c keeps its relaxed warning flags (already configured in meson.build's vendored_c_args).
