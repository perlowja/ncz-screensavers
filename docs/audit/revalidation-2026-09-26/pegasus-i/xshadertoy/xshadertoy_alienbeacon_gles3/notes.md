# xshadertoy_alienbeacon_gles3

**Classification:** GOOD
**GPU:** Intel UHD (CML GT2), Mesa 26.1.6
**Frame count:** 2 captured (sweep timed out at 25s)
**Coverage:** 100.0%   **Saturation:** 78.9%   **Motion:** 95.6%   **Intensity:** 122.9
**Frame_ms (60fps baseline):** 933.3
**RENDERER:** `Mesa Intel(R) UHD Graphics (CML GT2)`
**Exit:** 137 (SIGKILL, hard timeout — expected)

## Verdict
Looks exactly like the canonical alienbeacon shader: a moody desert landscape with a
floating cyan/blue beacon rod in the foreground and a magenta-tinted sky. Full coverage,
strong chroma, near-total inter-frame motion. The only reason nframes is so low (2) is
that the harness dumps every 60th frame, and the 25s sweep budget lets two dumps land
before timeout. Functionally perfect.

## Evidence
- `small_00.png` — early frame
- `small_01.png` — later frame, motion differs sharply from frame 0
- `metrics.json` — coverage/saturation/motion computed from raw framebuffer
