# skyrocket_gles3

**Classification:** BROKEN (partial render)
**GPU:** Intel UHD (CML GT2)
**Frame count:** 25 captured (25 valid frames after /tmp-full re-capture)
**Coverage:** 0.4%   **Saturation:** 0.3%   **Motion:** 0.8%   **Intensity:** 77.9
**Frame_ms:** 997.2
**RENDERER:** `Mesa Intel(R) UHD Graphics (CML GT2)`
**Exit:** 124 (timeout)

## Verdict
The first captured frame (`small_00.png`) is entirely black — rocket hasn't
launched yet. Later frames (small_04 through small_08) show colorful firework
bursts: a green particle cluster with trails, a yellow burst falling into red
particles, and a multi-stage effect with yellow → green → red. So the rocket
animation DOES run, but the bursts are localized to a tiny region of the
frame; coverage stays at 0.4% because most of the canvas remains black.
This is more partial-render than BROKEN — the renderer is producing the
intended visual but at a tiny scale relative to the 1920×1080 surface.
Classified BROKEN because the visible region is so small that a real
screensaver deployment would feel empty.

Note: initial sweep captured 17 frames with 8 of them being 0-byte (PEGASUS
/tmp was full from earlier captures — see the recovery commit). After
`rm -rf /tmp/cap-*` on the remote, re-capture produced 25 valid frames.

## Evidence
- `small_00.png` — all black (pre-launch)
- `small_04.png` … `small_08.png` — colorful firework bursts (green, yellow, red)
- `metrics.json` — 25 valid frames, nframes_total=25
