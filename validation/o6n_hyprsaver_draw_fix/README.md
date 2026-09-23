# O6N hyprsaver black-render investigation — 2026-09-23

O6N was at the `_greetd` Wayland greeter when tested; the `mini` user
had no `/run/user/1000/wayland-0` socket. Tests used the active greeter
compositor at `/run/user/981/wayland-0`, on the connected 3840×2160
display. Both binaries used the Mali-G720 GLES 3.2 renderer. No reboot
or display configuration change was made.

## Before

- `aurora-before.png`: `grim` two seconds after launching
  `hyprsaver_aurora_gles3`. 8,294,297 of 8,294,400 pixels are RGB
  `(0, 0, 0)`; the other 103 pixels are cursor detail.
- A second hyprsaver hack, `hyprsaver_blob_gles3`, produced the same
  all-black result before the fix.
- In a temporary diagnostic build, `glIsTexture(palette_lut_tex)` was
  true at first draw, and `glGetError()` after each texture activation,
  binding, sampler uniform, and blend uniform returned zero. The
  framebuffer center read `(0, 0, 0, 255)` after `glDrawArrays()`.
  The full generated aurora shader is in `aurora-prepared.frag`; its
  `palette()` helper is called from the fragment color path.
- Replacing the shader output with solid red still read back black.
  A red `glClear()` did read back red. This isolated the problem to
  drawing primitives, rather than palette data or Wayland alpha.

The linked `gles3_compat.c` defines `glDrawArrays()` as a legacy GL1
client-array shim. With no legacy client array registered, it returns
without drawing. The hyprsaver VBO call hit that shim. The compat
runtime already resolves the real libGLESv2 function for its own
immediate-mode draws; the fix exposes that native call to the
hyprsaver wrapper. The wrapper also disables inherited depth testing
for its fullscreen quad.

## After

- `aurora-after-2s.png`: 80,857 distinct RGB colors; center pixel
  `(168, 121, 94)`; only 50 black pixels.
- `aurora-after-4s.png`: 75,454 distinct RGB colors; center pixel
  `(144, 105, 73)`. Compared with the 2-second shot, 7,398,044 pixels
  changed, confirming animation.
- `blob-after.png`: 83,987 distinct RGB colors; center pixel
  `(160, 255, 0)`; only 50 black pixels.

All four PNGs are unmodified full-resolution `grim` captures. The
remaining black pixels in the color captures are part of the cursor.
The local Meson/Ninja build completed successfully after the source fix.
