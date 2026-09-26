# Fix the 4 real GLES3 screensaver failures found in cross-platform validation

## Context

Repo `ncz-screensavers`, branch `master`, current HEAD after commit `c6f41fb`
(the frame-callback race fix) and `76fa534` (design doc). Real cross-platform
validation (Sky1/Panthor, AMD64/AMD, AMD64/Intel) just ran for the first time
across all 90 GLES3-native hacks. 86 PASS. 4 real failures, all on O6N/Panthor
so far (haven't cross-checked against AMD64 yet — do that as part of this
task, see "Verification" below):

- `jigsaw_gles3`: exit 134, `free(): invalid pointer`, AFTER 5 real frames
  render correctly (`frame_done #0` through `#4` all logged before the
  crash). Not an init-time bug — something about ongoing per-frame state
  corrupts the heap. `jigsaw` is wired with `-DHAVE_JWZGLES` (GLU
  tessellator fallback, see `meson.build` line ~679) and links
  `src/normals.c src/rotator.c src/spline.c src/yarandom.c`. Look hardest
  at whatever per-frame allocation/free jigsaw or its GLU-shim path does —
  a double-free or use-after-free that only manifests after a few
  iterations is the classic shape.
- `highvoltage_gles3`: exit 139 (SIGSEGV), crashes on the FIRST draw call,
  zero diagnostic output beyond "initial draw" (not even a GL error
  printed). `highvoltage.c:226` calls `glFrustum(fw1, fw2, ...)` — per
  this project's own PORTED.md notes, highvoltage's frustum width is
  "per-frame variable, driven by the audio analyzer." In this headless
  test harness there is no real audio input. Check what the audio
  analyzer's output actually is when unfed (likely 0, NaN, or
  uninitialized) and whether that produces a degenerate frustum (e.g.
  `fw1 == fw2`, zero width) that crashes Panthor's driver computing the
  projection matrix. Fix at the hack level (clamp/guard against a
  degenerate audio reading) — don't paper over it in the GL shim, since a
  real system will also have moments of silence/no audio input and needs
  to survive that.
- `hexstrut_gles3`: exit 139 (SIGSEGV), same "dies with zero diagnostic
  output right after initial draw" shape as highvoltage but does NOT call
  glFrustum (grepped, confirmed absent) — a different root cause, don't
  assume it's the same bug. Wired with `src/rotator.c src/yarandom.c`
  (`meson.build` ~line 673). Needs its own investigation — check for
  anything else uninitialized-state-dependent in `hexstrut.c`'s init/draw
  path, and whether `rotator.c`/`yarandom.c` have any global/static state
  another hack might also touch (shared file, check for cross-hack state
  pollution if multiple hacks run in the same test process — they
  shouldn't, each `_gles3` binary is separate, but check regardless).
- `mapscroller_gles3`: NOT actually a rendering bug — real evidence: it
  renders 180+ real frames successfully (`frame_done #0` through `#180`
  logged) despite an early, tolerated
  `running mapscroller.pl: No such file or directory` (a missing Perl
  helper the hack degrades gracefully without). The actual problem is it
  doesn't exit cleanly on SIGTERM, needing the harness's SIGKILL fallback
  (classified HANG because of that, not because it's frozen). Find why —
  likely a signal handler installed by the hack itself overriding or
  blocking the harness's own `on_signal()`/`SIGTERM` handling (check for
  any `signal()`/`sigaction()` call in `mapscroller.c`, or a blocking
  syscall — the missing-perl-subprocess codepath is the most likely place
  something is left in an uninterruptible wait).

## Task

1. Root-cause and fix all 4, for real — reproduce each crash/hang yourself
   first (`./build/<name>_gles3` under a real Wayland session on ULTRA or
   O6N) before touching code, per this project's own standing discipline:
   a build that compiles is not evidence a fix works.
2. Rebuild the 4 fixed binaries and re-run them for at least 10 real
   seconds each (longer than the original ~4s test window that caught
   jigsaw's post-5-frame crash) — confirm no crash/hang for real.
3. **Verification is cross-platform, not just O6N.** These 4 failures were
   only measured on Sky1/Panthor so far. Before declaring this done,
   rebuild and test all 4 on at least one AMD64 host too (MEDUSA,
   `sshpass -p medusa ssh medusa@192.168.207.84`, or PEGASUS, `sshpass -p
   pegasus ssh pegasus@192.168.207.85` — both have working toolchains and
   real hardware acceleration confirmed this session) to see whether these
   are Panthor-specific or universal bugs. Report which.
4. Update `docs/CROSS-PLATFORM-GLES3-VALIDATION-2026-09-22.md` if it
   exists yet (check — a prior zoder dispatch on this same task froze
   partway through and may not have written it; if it doesn't exist,
   don't worry about it, that's a separate deliverable) or otherwise just
   report the real before/after status of these 4 hacks clearly.

## Hard constraints

- Real reproduction and real re-test only — no "should be fixed now"
  without actually running the binary and watching it not crash for a
  real duration.
- `jigsaw`/`hexstrut` fixes must not touch shared shim files
  (`src/gles3_compat.c/.h`, `src/xscreensaver_compat.c/.h`) unless the
  root cause genuinely lives there — if it does, say so explicitly and
  make sure the fix doesn't regress any of the other 86 passing hacks
  (rebuild and spot-check a few unrelated hacks that use the same shared
  file if you touch it).
- Commit each real fix separately (one commit per hack, don't bundle all
  4 into one commit) — push to
  `ssh://root@192.168.207.101/mnt/datapool/git/ncz-screensavers.git`
  (the `jasonperlow@` form is pubkey-only and will fail; use `root@` with
  `GIT_SSH_COMMAND='sshpass -p "Gumbo@Kona1b" ssh -o
  PubkeyAuthentication=no -o StrictHostKeyChecking=no'`). Re-fetch and
  rebase before every push.

## Verification setup — TYDEUS pair

Author with MiniMax-M3, review with TYDEUS's local reviewer slot
(`--reviewer reviewer`, re-probe `http://192.168.207.73:8006/v1/models`
before trusting a cached claim about which model backs it). Wire a real
`--check` — at minimum, something that rebuilds all 4 binaries and greps
their stderr for a crash signature over a real multi-second run, e.g.:

    for h in jigsaw hexstrut highvoltage mapscroller; do
      timeout 10 ./build/${h}_gles3 > /tmp/${h}.out 2>&1
      rc=$?
      [ $rc -eq 137 -o $rc -eq 139 -o $rc -eq 134 ] && exit 1
    done
    exit 0

(adjust for the real Wayland env vars this needs — check `run-on-host.sh`
for the pattern). `--agent-timeout 7200`, `--loop-timeout 10800`.
