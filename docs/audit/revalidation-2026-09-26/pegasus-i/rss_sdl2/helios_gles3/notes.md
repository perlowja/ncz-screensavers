# helios_gles3

**Classification:** BROKEN
**GPU:** Intel UHD (CML GT2)
**Frame count:** 18
**Coverage:** 1.7%   **Saturation:** 1.6%   **Motion:** 9.1%   **Intensity:** 96.1
**Frame_ms:** 996.1
**RENDERER:** `Mesa Intel(R) UHD Graphics (CML GT2)`
**Exit:** 137

## Verdict
Coverage 1.7%, sat 1.6%, motion 9.1% (slightly higher than the other broken rss-sdl2
targets — the cursor markers do move a little). Visual inspection of `small_09.png`:
black canvas with a few colored dots (cyan/magenta/blue) drifting around. The
intended render is a "helios" solar visualization with a bright sun and orbiting
particles. Same rss-sdl2 family defect: only the cursor widget renders, but the
widget itself is multi-colored in this case. BROKEN.

## Evidence
- `small_00.png` … `small_17.png` — eighteen sample frames
