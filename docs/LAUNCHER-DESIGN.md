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
 ncz-screensaver-settings (GTK4 + libadwaita, python3-gi): chooser/timeout/per-hack/color/preview
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
