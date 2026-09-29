# NCZ-OS screensaver launcher, idle daemon and settings UX - design

Status: MVP design, 2026-09-29. Owner: launcher/, tools/, config/ (LEAD 2).
The hack renderers (src/, vendor/) and packaging are owned by the consolidation
lead; this document only fixes the interfaces between them.

## 1. What the four test hosts actually run (investigated live, read-only)

| Fact | CHIMERA .6 / MEDUSA .20 | PEGASUS .85 | O6N .3 |
|---|---|---|---|
| CPU/GPU | x86_64, MacBookPro16,1: Navi14 (radeonsi, GLES 3.2 Mesa 26.1.6) + Intel iGPU | x86_64, RTX 2060M + Intel CML iGPU (default EGL = Intel Mesa) | aarch64, Sky1 (session GPU env from `ncz-gpu-env`; a bare ssh shell sees llvmpipe) |
| Session | greetd -> `singularity-labwc-session` -> `/opt/singularity/bin/labwc` (wlroots 0.20) -> `singularity-desktop` shell | same | same |
| Wayland socket | `/run/user/1000/wayland-0` | same | same |
| Idle daemon present | none (no swayidle, no wlopm) | none | swayidle + `ncz-idle-manager` (older cix-installer 57/58 design) |
| Screensaver bits installed | none | none | `ncz-screensaver-run`, `dev.ncz.screensaver` schema, `hacks.tsv` |
| Lock | `/opt/singularity/bin/singularity-lockscreen` (ext-session-lock-v1), `ncz-lock` wrapper on O6N | same | same |
| Outputs | one (eDP-1) | one (eDP-1, 4K) | one (DP-2, 4K) |
| Tools | grim, wlr-randr, python3-gi, gir1.2-adw-1, imagemagick | same | same + meson/ninja/valac |

Wayland globals advertised by the compositor (verified on CHIMERA): `ext_idle_notifier_v1` v2,
`zwp_idle_inhibit_manager_v1`, `ext_session_lock_manager_v1`, `zwlr_layer_shell_v1` v4,
`zwlr_output_power_manager_v1`, `zwlr_screencopy_manager_v1` v3,
`ext_image_copy_capture_manager_v1`, `zwlr_virtual_pointer_manager_v1`,
`zwp_virtual_keyboard_manager_v1`. The virtual pointer/keyboard globals let the test
harness inject real input inside the session.

How the hacks present themselves today (read from `src/wl-screenhack.c`): each hack
is its own process (`/usr/lib/ncz-screensavers/<id>`), creates a `zwlr_layer_shell` surface on
the OVERLAY layer, all four anchors, exclusive zone -1, EXCLUSIVE keyboard
interactivity, and quits on a key press. Only one `wl_output` is used. So a hack is already
a correct "screensaver surface"; the launcher must not add another shell protocol.

## 2. Decision: minimal robust MVP

Rejected: session-lock as the saver surface (a crashed client leaves the session
permanently locked; labwc keeps `locked` true with no client), wayshade, GNOME/X11 style
daemons, swayidle (not installed on 3 of 4 hosts; extra moving parts).
Chosen: three small programs plus one systemd user unit, all keyed on the existing
`dev.ncz.screensaver` GSettings schema so the already-written Singularity
"screensaver-chooser" plugin keeps working (schema is extended, never broken).

```
 systemd --user                       GSettings dev.ncz.screensaver (dconf)
   ncz-screensaver-idled  (C) <------- live "changed" signals
     |  ext-idle-notify-v1: timer A (start saver), B (lock), C (display off)
     |  zwlr-output-power-management: DPMS off/on
     |  logind PrepareForSleep/Lock: lock
     |-- idled(A)   -> exec `ncz-screensaver start --idle`
     |-- resumed(A) -> exec `ncz-screensaver stop`
     |-- idled(B)   -> `ncz-lock`   (stops the saver first)
     '-- idled(C)/resumed(C) -> DPMS off/on
 ncz-screensaver (Python 3, stdlib only): supervisor/launcher/CLI
     start|stop|status|list|preview|set-timeout|set-hack|set-mode|set-color|enable|disable|config
 (settings UX: the Singularity plugin on NCZ-OS; the GTK4 app is now the reference application in contrib/)
```

### Idle detection and dismissal
Compositor-side `ext_idle_notification_v1` (v1 request, which honors idle inhibitors)
gives both "idle" and "resumed". "resumed" fires on ANY seat input (key, pointer motion,
touch), so dismissal is universal and does not depend on the hack handling input. The
hack's own quit-on-key is a second, independent path. Idle inhibitors (video players,
Singularity caffeine, browsers) are enforced by the compositor: while inhibited the idle
notification never fires, so no extra code is needed. Harness proves this with a
zwp_idle_inhibit client.

### Launcher behavior (`ncz-screensaver start`)
- Single instance: `flock` on `$XDG_RUNTIME_DIR/ncz-screensaver/lock`; state file
  `state.json` (supervisor pid, hack id, pgid, start time, failure counters).
- Selection by `mode`: `off` | `one` (hack-id) | `random` (pool = `random-hacks`, empty = whole
  catalog; never repeats the previous hack) | `playlist` (`playlist` order). Rotation every
  `cycle-delay` seconds for random/playlist.
- Hack process runs in its own session/process group; stop = SIGTERM to group, 2 s grace,
  then SIGKILL. Supervisor removes state on exit.
- Safety net: a hack that exits within 3 s, or dies of a signal, counts as a failure; the
  supervisor immediately moves to the next candidate; 3 consecutive failures ends the
  session quietly (the idle chain proceeds to lock/DPMS) and a per-hack failure record under
  `$XDG_STATE_HOME/ncz-screensaver/failures.json` benches a repeatedly failing hack for 24 h.
- Black-frame guard (`verify-render`, default on when `grim` exists): ~4 s after start, grab
  a downscaled screenshot via screencopy; an entirely black frame counts as a failure.
- Environment: derives `WAYLAND_DISPLAY`/`XDG_RUNTIME_DIR` when missing, passes
  `NCZ_BLACKHOLE_COLORS` for `blackhole_gles3` from `blackhole-color-mode`
  (`stylized` = variable unset; `kipthorne`; `faithful`; anything else passed through).
- Multi-monitor MVP: the hack covers the output the hack picks (first output); idled
  DPMS handles all outputs. Per-output hack instances need an `NCZ_OUTPUT` option in the
  hack harness (open item for src/).

### Lock and DPMS interplay
`lock-delay` is counted from saver start (existing semantic); with `mode=off`, lock happens at
`hack-idle-delay`. `ncz-lock` (wrapper installed by cix-installer, or a built-in fallback
that calls `singularity-lockscreen`) is invoked by idled and always calls
`ncz-screensaver stop` first. `display-off-delay` (0 = never) is absolute idle seconds.

### Config schema (config/dev.ncz.screensaver.gschema.xml, superset of the current one)
Existing keys keep names, types and semantics: `mode` (off/one/random + new `playlist`),
`hack-id`, `random-hacks`, `hack-idle-delay`, `cycle-delay`, `lock-enabled`, `lock-delay`,
`lock-on-suspend`. New: `playlist` (as), `blackhole-color-mode` (s), `display-off-delay` (i),
`verify-render` (b). Ranges lowered to min 1 s so the test harness can use short timers.

### Testability
`GSETTINGS_BACKEND=keyfile` plus `XDG_CONFIG_HOME` runs idled and the launcher against
an isolated config with no dconf. `NCZ_SCREENSAVER_DIRS` overrides the binary search path,
`NCZ_SCREENSAVER_CATALOG` the catalog. `ncz-screensaver-idled --dry-run` prints the notification
plan without acting.

## 3. Out of scope (per operator)
GPU probing/fidelity tiers, wayshade, ISO builds, kernel changes.

## 4. As built and verified (2026-09-29)

Files: `launcher/ncz-screensaver` (Python launcher and CLI), `launcher/ncz-screensaver-idled.c` (idle daemon),
`launcher/ncz-screensaver-idled.service`,
`launcher/ncz-screensaver-run-compat` (old `--start/--stop/--preview` interface), `config/dev.ncz.screensaver.gschema.xml`,
`debian/ncz-screensavers.postinst` (disables the older `ncz-idle-manager` user unit), `tools/host-test.sh` with
`tools/host-test-agent.py`, `tools/wl_poke.py` (raw-wire Wayland client: virtual pointer, virtual keyboard, idle inhibitor,
registry dump), `tools/gen-host-results-md.py`. Results: `docs/HOST-TEST-RESULTS.md`.

Decisions confirmed or changed while building:

- The daemon uses the plain `ext_idle_notification_v1` request, so idle inhibitors (proved with a `zwp_idle_inhibit_v1`
  client) hold the saver off and DPMS uses `zwlr_output_power_v1` directly; neither swayidle nor wlopm is needed.
- The hack environment matters: a systemd user service does not inherit the compositor's GPU variables (the O6N Mali
  EGL vendor file and `LD_LIBRARY_PATH`), so the launcher copies them from the compositor's `/proc/PID/environ`
  (never `DRI_PRIME` or NVIDIA offload variables, never names containing password, token or secret).
- Stop protocol: SIGTERM to the hack's process group, 1 s grace, then SIGKILL (a hack blocked in `eglSwapBuffers`
  behind a lock surface ignores SIGTERM on Mali). The launcher does not start a hack while a lock screen runs.
- Lock chain: on the lock timer the daemon runs `ncz-screensaver stop`, waits for it, then the lock command
  (`NCZ_LOCK_CMD`, `ncz-lock`, or `singularity-lockscreen`).
- Multi-monitor: hacks bind one output; DPMS covers all outputs. Per-output hack instances need an output option in the
  hack harness (open item for `src/`). All four test hosts have a single output, so this is untested.
- Not done by design: GPU probing or fidelity tiers, wayshade.

Bugs found by the harness and by adversarial review and fixed: alias-unsafe settings snapshot (crash at start), main loop
never quitting on SIGTERM, missing stop on input resume, wrong `Inhibit` signature, dangling `GPollFD` and a busy
`GSource` check, `xdg` and `wl_surface` opcodes and the invalid XKB keymap in the test client, PPM header parsing that
ate whitespace-valued pixels (false zero coverage), destroyed `zwlr_output_power_v1` proxy kept in the array, unbalanced
`wl_display_prepare_read`, leaked idle-notification data, and a hang on truncated PPM input in the black-frame guard.

## 5. The display stays black: session locked, no lock process

labwc keeps an `ext-session-lock-v1` session locked when the lock client dies without unlocking, so the screen stays
black and nothing draws a prompt (`pgrep -f singularity-lockscreen` finds nothing). The idle daemon now restarts a lock
command that exits abnormally (nonzero status or signal, up to 5 times, one second apart), which covers a crashing or killed
locker. A clean exit means the user unlocked. Only the compositor's own locker is ever started for locking; the saver never
locks by itself, and the harness recovers a blank display before timing anything.

Recovery over ssh, without a reboot (any host, as the desktop user):

```sh
export XDG_RUNTIME_DIR=/run/user/$(id -u) WAYLAND_DISPLAY=wayland-0 DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/$(id -u)/bus
# 1. Take over the orphaned lock with a fresh client (the protocol allows it).
setsid nohup /opt/singularity/bin/singularity-lockscreen >/tmp/lock.log 2>&1 </dev/null &
# 2. Unlock it by typing the account password. From the keyboard, or through the virtual keyboard
#    (evdev codes: q=16 w=17 e=18 r=19 t=20 y=21 u=22 i=23 o=24 p=25 a=30 s=31 d=32 f=33 g=34 h=35 j=36
#    k=37 l=38 z=44 x=45 c=46 v=47 b=48 n=49 m=50, digits 1..9=2..10 and 0=11, Enter=28):
python3 tools/wl_poke.py motion; python3 tools/wl_poke.py key --codes 50,23,49,23,28   # example: "mini" + Enter
```

If the compositor does not answer at all, `sudo systemctl restart greetd` ends the session and returns to the greeter
(this closes every application). `wlr-randr --output NAME --off; wlr-randr --output NAME --on` re-enables a panel that a
crashed DPMS client left off; the idle daemon also powers all outputs on when it exits.

## 6. Singularity Settings plugin: "Screensaver and Lockscreen"

`plugin/screensaver/` is a normal Singularity plugin (libpeas extension of `Singularity.Plugin`, laid out like
`sensors` and `wallpapers-*` in singularity-plugins): `screensaver.plugin`, `screensaver.vala` (UI), `backend.vala`
(generic `ScreensaverBackend` contract), `ncz_backend.vala` (the only NCZ-specific file). It is built on libsingularity
widgets only (`PreferencesGroup`, `SwitchRow`, `SpinRow`, `SelectionRow`, `ActionRow`, `StatusPage`); no libadwaita.

- **Where it appears.** The shell has no plugin API for adding a top-level Settings page yet, so the page is the plugin's
  settings widget, reached exactly like the other plugins: Settings > Plugins > "Screensaver and Lockscreen" (gear), and it
  is found by Settings search ("Screensaver and Lockscreen, Plugins / Active Plugins"). When the shell grows an
  `add_settings_page` hook, `ScreensaverSettings` is a self-contained widget that can be hosted as its own page unchanged.
- **Enabling.** Like every plugin it is off until listed in `dev.sinty.desktop enabled-plugins` (the test hosts have
  `'screensaver'` appended; the distro override should list it next to `'sensors'`).
- **Gate.** `is_available()` checks the settings schema, the catalog, the `ncz-screensaver` launcher and the idle daemon
  binary; otherwise the page shows a status page instead of controls.
- **No write-only settings.** Every control has a consumer: mode, chooser, pool, start and change delays, display-off,
  color model and GPU offload are read by `ncz-screensaver` / `ncz-screensaver-idled`; the lock switch, lock delay and lock
  on suspend are the shell's own `dev.sinty.lockscreen` keys (the shell is the locker), so `dev.ncz.screensaver lock-enabled`
  now defaults to false and the idle daemon only locks when explicitly asked. The playlist mode has no editor and is not offered.
  The GPU row only appears on a hybrid NVIDIA machine.
- **Build.** `tools/build-plugin.sh SDK_PREFIX OUT_DIR` builds against a Singularity SDK (headers, vapi and pkg-config file
  from `/opt/singularity`; libsingularity itself is only linked by soname, so the same recipe builds on amd64 and arm64).
  The main `meson.build` builds it with `-Dsingularity-plugin=enabled`; it installs `libscreensaver.so` (0755) and
  `screensaver.plugin` (0644) into `/opt/singularity/lib/singularity/plugins/screensaver/`, the layout of the siblings.
- **Evidence.** `docs/ux-shots/` (CHIMERA, MEDUSA, PEGASUS after a greetd restart and scripted login): settings search
  result, the Plugins list with the entry, and the plugin page including the black hole, graphics (PEGASUS only) and per-hack
  groups. Clicking the switches and spin buttons changes `dev.sinty.lockscreen` and `dev.ncz.screensaver` as expected.

## 7. GPU classes, offload, per-hack options, render quality, host lock

The GPU work has its own reference, `docs/GPU-CLASSES.md`: calibration, the weak, mid and
strong classes, the measured per-hack requirements in `tiers.tsv`, how each system presents
the catalog, vendor-agnostic per-process offload and the tests. This section keeps the rest of
the launcher contract.

**Per-hack options.** A hack with `options/<id>.tsv` (columns: name, type `bool|int|float|enum|string`, default, min, max,
choices, label, description, group, env name) gets user values from `dev.ncz.screensaver hack-options` (`a{sa{ss}}`),
validated by the launcher and passed as environment variables to that process only; unknown names and out-of-range values
are dropped with a warning. Values arrive as environment, so they override the hack's own config file.
`ncz-screensaver list-options|get-option|set-option|reset-options` manage them, `preview HACK --option name=value` tries
values without storing them, and the legacy `blackhole-color-mode` fills `palette` when no palette option is stored. Both
UIs generate their controls from the schema. `options/_render.tsv` is the common schema for every shader hack.

**Render quality.** `render-scale-mode` (auto|fixed), `render-scale` and `max-render-height` reach shader hacks as
`NCZ_RENDER_SCALE_MODE`, `NCZ_RENDER_SCALE` and `NCZ_MAX_RENDER_HEIGHT` (an explicit 0 means unlimited, unset means the
platform default). Auto applies platform data (`render-defaults.tsv`: 1080 cap on Sky1 and on a large integrated-GPU
display), a render scale of 0.5 on a weak-class GPU and the per-hack scale from `render-hints.tsv` on a mid-class GPU;
High (fixed, 1.0) overrides all of them. The UI presets are Auto, High, Balanced, Fast plus an advanced height field.

**Environment and cache.** A hack process gets the compositor's GPU environment (EGL vendor file, library path, backend
selection), so it never falls back to llvmpipe; an exit status of 3 means the hack's software-renderer guard refused and the
launcher never retries or benches it. The Mesa shader cache stays on, in `$XDG_CACHE_HOME/ncz-screensavers/mesa`. The
one-time black-frame check runs once per boot per hack. `ncz-screensaver doctor` prints the hack environment against the
unit environment, cache size, unit resource limits, the GPU classes and the effective offload plan.

**Host lock.** `tools/host-lock.sh` takes an exclusive per-host lock (`acquire build|test WHO WHAT`, atomic `mkdir`,
refresh, release, status; a stale lock is reported and never removed silently). `tools/host-test.sh` takes it in test mode,
waits for a load below 2, runs the agent under `timeout`, refreshes the lock and cleans up on exit. Builds take it in build
mode, run under `nice -n 10` with at most six jobs, and never on a host that is being tested.

**Transport and drivers.** `tools/host-test.sh` connects with password authentication only (`sshpass -e`, forced, no key
fallback, host keys checked). The harness records the kernel GPU driver (Mali: `mali_kbase` or `panthor`) in every result
and fails an aarch64 run when only `panthor` is loaded unless `--allow-panthor` is given: the open driver reports GLES 3.1
and the hacks need 3.2 shader syntax, so numbers from it are not comparable.

**Broken and suspect hacks.** `assets/screensaver-chooser/broken.tsv` (id, status broken|suspect, reason, date, tracking)
keeps listed hacks out of every pool, marks them in `list` and both UIs, and still allows an explicit run. The harness
flags a hack that is lit and moving but looks wrong (flat fill, one dominant color, tiny motion) as REVIEW instead of PASS
and builds per-host contact sheets with `tools/make-contact-sheet.py`.
