# O6N Wayland socket discovery fix — 2026-09-25

The prior remote validators forced
`XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-0`.  That made every
target look broken when O6N was at its `_greetd` greeter, whose compositor was
observed at `/run/user/981/wayland-0`.

`validation/find-wayland.sh` now enumerates `/run/user/*/wayland-*` (so neither
the UID nor socket suffix is assumed) and accepts a candidate only after
`wlr-randr` or `wayland-info` completes a real Wayland roundtrip.  Explicitly
provided environment values retain priority when they are live.  The local,
remote, and full-sweep entry points consume the discovered pair.

## Before evidence

`before/` contains the 50 stderr files from historical commit `e0637f4`.  All
50 contain `gles3_harness: wl_display_connect failed`, including all 13 RSS
ports named in the dispatch and the entire associated Round-13/15 batch.  The
files are retained verbatim rather than reconstructing a failure that the
board's current session state no longer presents.

## After evidence

O6N was reachable at `mini@192.168.207.3` and reported `aarch64`.  At test time
the live compositor happened to be `/run/user/1000/wayland-0`; the helper found
and roundtrip-probed it without assuming either value.  `after/results.jsonl`
and `after/run.stderr` record **50 pass / 0 fail** from the corrected remote
path.  Every `after/remote_o6n_*.stderr` file contains
`RENDERER=Mali-G720-Immortalis`, shader compilation, and frame-progress output.

This verifies the connection failure was orchestration state, not a rendering
failure.  Full visual/animation classification is intentionally handled by the
separate two-capture full-matrix sweep.
