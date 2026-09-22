# Cross-platform GLES3 screensaver validation: Sky1 vs AMD64/AMD vs AMD64/nvidia

## Context

Repo: `ncz-screensavers`, canonical `root@192.168.207.101:/mnt/datapool/git/ncz-screensavers.git`,
branch `master`, currently at real commit `acd1061` (verify `git log -1` still
matches or is ahead before starting — this is a live repo other sessions may
also be pushing to).

**The porting work is essentially DONE, not starting.** As of `acd1061`:
90 of 94 total hacks are GLES3-native (`<name>_gles3` binaries, no gl4es
translation shim, linked directly against system libGLESv2/libEGL). Only 4
remain deferred, all with real, documented, non-trivial technical blockers
(GLU tessellator gaps mostly) — do NOT attempt to port those 4 as part of
this task; this task is VALIDATION, not more porting.

**What has NOT been done**: systematic cross-platform validation. Prior
verification (PORTED.md's own commit history) was almost entirely on O6N
(Sky1/Panthor arm64) alone, via `grim` screen captures, hack-by-hack as they
were ported. Nobody has done a real comparative pass across Sky1 (arm64,
Panthor GPU, Mesa) vs AMD64/AMD (MEDUSA, T2 MacBook, AMD Navi14 iGPU,
amdgpu/RADV) vs AMD64/nvidia (PEGASUS, RTX 2060 Mobile, nvidia proprietary
or nouveau — check which driver is actually loaded, don't assume). That
3-way comparison is the actual deliverable here.

## Test hosts — real, live NCZ-OS installs, not generic dev boxes

- **O6N** (192.168.207.3): Sky1 arm64, Panthor GPU. `user mini, password mini`
  — NOT the fleet password. Boot-testing/reboots on this box are
  human-supervised only (see `~/.claude/rules/kernel-build-checklist.md` —
  do not trigger an unattended reboot; running screensaver binaries on an
  already-booted system is fine, rebooting it is not).
- **MEDUSA** (192.168.207.86): amd64, T2 MacBook, AMD Navi14 iGPU. Real
  NCZ-OS install (kernel `7.2.6-2-t2-trixie`). `user medusa, password
  medusa`. `sudo -S` needs the SAME password (not NOPASSWD, not the fleet
  password).
- **PEGASUS** (192.168.207.85): amd64, RTX 2060 Mobile laptop (also has an
  Intel CometLake-H iGPU — confirm which GPU actually renders Wayland/GLES
  before assuming it's the nvidia one). Real NCZ-OS install, same kernel
  line as MEDUSA. `user pegasus, password pegasus`. `sudo -S` needs the
  SAME password.
- `meson`/`ninja`/`libgles2-mesa-dev`/`libegl1-mesa-dev`/`libwayland-dev`
  already installed on both MEDUSA and PEGASUS this session — verify
  present, don't reinstall blind.

## What to build and where

- **arm64 binaries**: build natively on ULTRA (arm64, this is where you're
  running from) OR directly on O6N if it has a usable toolchain — check
  before assuming; O6N is a real installed NCZ-OS system, may or may not
  have build-essential/meson/ninja. Deploy the resulting binaries to O6N
  for testing (scp, or build there directly if O6N can build — cheaper if
  it can, since arm64 cross-build from ULTRA to O6N should be
  binary-compatible either way, both real Sky1/arm64).
- **amd64 binaries**: MEDUSA and PEGASUS are both real amd64 hardware with
  toolchains now installed. Build NATIVELY on each (don't cross-compile
  from ULTRA — ULTRA is arm64, would need cross toolchain for no real
  benefit when the target hardware itself can build in minutes).

## The actual validation task

For all 90 GLES3-native hacks, on ALL THREE platforms:

1. Build clean (`meson setup build && ninja -C build`) — record any hack
   that fails to LINK per-platform (a link failure on one platform but not
   another is itself a real, reportable finding — e.g. a GLES extension
   present on Mesa/Panthor but absent on nvidia's GLES implementation, or
   vice versa).
2. Run each `_gles3` binary for a bounded time (a few seconds is enough to
   confirm it's actually rendering, not crashing/hanging) under a real
   Wayland session (labwc, per this project's compositor) on each machine.
   Capture a `grim` screenshot per hack per platform — this is the same
   evidence method already used for the O6N-only verification, extend it
   to all three.
3. Classify each hack x platform combination into one of: **PASS** (renders
   correctly, matches what O6N/Panthor already showed for hacks with prior
   O6N evidence), **VISUAL DIFFERENCE** (renders, but genuinely looks
   different — describe how: wrong colors, missing geometry, different
   lighting, wrong scale/aspect — this is real signal about a GLES
   implementation difference, not noise to discard), **CRASH** (segfault,
   abort, GL error abort), **BLACK/BLANK** (process runs, no visible
   output), or **HANG** (doesn't return/exit within a reasonable timeout).
4. **The real question to answer**: is there a pattern by GPU VENDOR
   (Panthor/Mali-derived vs AMD/RADV vs nvidia) or by ARCHITECTURE
   (arm64 vs amd64) in which hacks fail or render differently? Concretely
   useful findings: "hacks using X GLES extension fail specifically on
   nvidia," "hacks assuming Y precision qualifier render differently on
   Panthor vs AMD," etc. — not just a pass/fail count. Look at
   `src/gles3_compat.c` / `src/gles3_compat.h` for anything that might be
   Mesa-specific (implicit assumptions from having been developed almost
   entirely against Panthor/Mesa) that could explain any nvidia-specific
   failures.
5. On PEGASUS specifically: confirm which GPU is actually in use for the
   Wayland session (`wlr-randr`, `eglinfo`, or checking the compositor's
   own GPU selection — laptops with hybrid Intel+nvidia graphics often
   default to the iGPU unless explicitly configured otherwise). If it's
   defaulting to the Intel iGPU rather than the RTX 2060, that is itself
   a real, useful finding (means we don't actually have live nvidia GLES3
   coverage yet, and getting the RTX 2060 into the loop is prerequisite
   work) — don't silently test the wrong GPU and report it as "the nvidia
   result."

## Output

A findings doc, `docs/CROSS-PLATFORM-GLES3-VALIDATION-2026-09-22.md`,
committed to the repo: a full hack x platform matrix (90 rows x 3 columns
minimum), plus a narrative section on any architectural/vendor patterns
found, plus explicit build-failure and crash logs (paste real output, not
paraphrases) for anything that didn't PASS. Commit real screenshots too
(or a representative sample if 270 individual images is too much — your
call, but be explicit about what's included vs sampled and why).

## Hard constraints

- Never reboot O6N unattended. Never trigger any install/reflash on any
  of the three hosts — this is read-only validation of already-built
  binaries against already-installed OSes.
- Real screen captures and real build logs only — no "should work," no
  invented pass/fail results. If a host can't run a given check for a
  real reason (no GPU access from SSH session, no active Wayland
  session), say so explicitly and either find a workaround (a nested/
  headless Wayland compositor, if one is reasonable) or report the gap
  honestly rather than skip it silently.
- Commit progress incrementally (per platform, not all-or-nothing at the
  end) — push to
  `ssh://root@192.168.207.101/mnt/datapool/git/ncz-screensavers.git`
  (the `jasonperlow@` form is pubkey-only and will fail; use `root@`
  with `GIT_SSH_COMMAND='sshpass -p "Gumbo@Kona1b" ssh -o
  PubkeyAuthentication=no -o StrictHostKeyChecking=no'`). Re-fetch and
  rebase before every push — this fleet has had real concurrent-push
  collisions this session on other repos.

## Verification setup — TYDEUS pair

Author with MiniMax-M3, review with TYDEUS's local reviewer slot:
`--reviewer reviewer` (re-probe `http://192.168.207.73:8006/v1/models`
before trusting a cached model claim — it rotates). Wire a real `--check`
(e.g. `ninja -C build -t targets | grep -c _gles3` should report 90, a
cheap sanity gate that the full port set is actually being tested, not a
subset). `--agent-timeout 7200`, `--loop-timeout 10800` — this task spans
3 hosts and real hardware testing, budget generously.
