# Render scale measurements (Mali-G720)

Host O6N (Cix Sky1, Mali-G720-Immortalis, mali_kbase; cmdline module_blacklist=panthor,...; 1920x1080 output), package 0.4.0, 72 shader hacks (Black Hole, 35 hyprsaver, 36 xshadertoy), each run 12 s under `timeout` with `--render-scale=S --render-scale-mode=fixed` at S = 1 and, for hacks under about 52 fps, also 0.75, 0.5, 0.35. fps is the steady-state frame count over about 7 s; p95 is the 95th percentile frame interval in ms (`[stats]` line). Raw lines: `docs/render-scale-o6n-sweep.txt`.

At native 1080p, 56 of 72 hacks hold 55 fps or more. 16 hacks are below 55 fps at scale 1; the table shows the effect of the render scale (fps, p95 ms):

| Hack | 1.0 | 0.75 | 0.5 | 0.35 |
|---|---|---|---|---|
| xshadertoy_bestill3-0_gles3 | 0 (0) | 39 (26) | 60 (17) | 60 (17) |
| xshadertoy_alienbeacon_gles3 | 5 (248) | 8 (154) | 17 (75) | 33 (37) |
| xshadertoy_bestill2-0_gles3 | 6 (84) | 21 (48) | 45 (22) | 60 (17) |
| xshadertoy_bestill4-0_gles3 | 12 (85) | 21 (49) | 45 (22) | 60 (17) |
| xshadertoy_downfall_gles3 | 14 (74) | 24 (42) | 52 (19) | 60 (17) |
| xshadertoy_polarnight_gles3 | 14 (80) | 24 (48) | 48 (25) | 60 (17) |
| xshadertoy_bestill1-0_gles3 | 15 (70) | 25 (41) | 54 (19) | 60 (17) |
| xshadertoy_skyline_gles3 | 17 (71) | 28 (44) | 55 (21) | 60 (17) |
| xshadertoy_noxfire_gles3 | 18 (57) | 31 (33) | 60 (17) | 60 (17) |
| xshadertoy_bestill0-0_gles3 | 22 (46) | 38 (27) | 60 (17) | 60 (17) |
| xshadertoy_fluxcore_gles3 | 23 (63) | 39 (37) | 58 (19) | 60 (17) |
| xshadertoy_universeball_gles3 | 25 (41) | 42 (25) | 60 (17) | 60 (17) |
| xshadertoy_rigrekt_gles3 | 30 (34) | 52 (20) | 60 (17) | 60 (17) |
| hyprsaver_lissajous_gles3 | 36 (28) | 60 (17) | 60 (17) | 60 (17) |
| xshadertoy_topologica_gles3 | 38 (26) | - | - | - |
| xshadertoy_batteredplanet_gles3 | 41 (25) | - | - | - |

Findings:

* Black Hole runs at 59 fps with p95 17.3 ms at native 1080p (adaptive ray-step tier medium).
* Halving the render height (scale 0.5) brings all but alienbeacon to 45 to 60 fps; alienbeacon needs 0.35 (33 fps, p95 37 ms) and is the heaviest shader in the set.
* The ladder in auto mode (1, 0.75, 0.5, 0.35, one step per 3 s while the 95th percentile frame time exceeds 40 ms) therefore converges for every hack in the set. `assets/screensaver-chooser/render-hints.tsv` lists, per slow hack, the smallest reduction that reaches 45 fps, so a launcher can start there and skip the ramp.
* On a 4K Sky1 panel the harness already caps the render height at 1080 (Mali default), so the render cost equals the 1080p case measured here. The MS-R1 (same SoC, 4K panel) sweep could not be completed: its desktop session ended during the run and the host was at the greeter.

Image quality: scale 0.5 renders a quarter of the pixels and is upscaled with the compositor's linear filter (wp_viewport); shader hacks are smooth gradients, so the loss is softness rather than artifacts; 0.35 is visibly soft.
