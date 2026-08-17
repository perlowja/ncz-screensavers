# ROUND 7 — coordinator verification checklist

This is the exact sequence the coordinator runs on Debian forky arm64
to verify round 7's deliverables (Part 1: GL4ES works; Part 2: a real
xscreensaver hack is ported through the pipeline).

**Time budget**: ~45 minutes including GL4ES build (which is the long
pole at ~10 min on the target hardware).

---

## 0. Prep — get the source tree clean

```bash
cd /path/to/ncz-screensavers    # wherever the coordinator cloned it
git status                      # confirm round 7 files are present
ls src/                         # expect:
                                #   gl4es_triangle.c
                                #   xscreensaver_compat.{h,c}
                                #   image_data_to_ximage.c
                                #   glmatrix_harness.c
                                #   glmatrix.c
                                #   images/gen/matrix3_png.h
                                #   wl-screenhack.c             (rounds 1-6, do not edit)
cat meson.build | head -30      # confirm options block visible
cat PORTING.md | head -50       # confirm round 7 doc exists
```

Expected: all of the above present; `git status` shows new files; wl-screenhack.c
unchanged from round 6.

---

## 1. Install GL4ES (Part 1 dependency)

```bash
sudo apt install -y libgl4es0 libgl4es-dev libpng-dev
ls /usr/lib/aarch64-linux-gnu/gl4es/libGL.so.1     # verify
ls /usr/include/gl4es/gl4eshint.h /usr/include/gl4es/gl4esinit.h
```

Expected: the Debian libGL.so.1 at the gl4es subdir. If the packages
aren't available, build from source per `PORTING.md §3a`.

---

## 2. Build the ncz-screensavers binaries

```bash
cd /path/to/ncz-screensavers
meson setup build --wipe
meson compile -C build
```

Expected output (zero warnings, zero errors):
```
[1/9] Compiling C object wl-screenhack.p/src_wl-screenhack.c.o
[2/9] Compiling C object gl4es_triangle.p/src_gl4es_triangle.c.o
[3/9] Compiling C object gl4es_triangle.p/wayland-client-protocol.c.o
[4/9] Compiling C object gl4es_triangle.p/xdg-shell-client-protocol.c.o
[5/9] Compiling C object gl4es_triangle.p/wlr-layer-shell-unstable-v1-client-protocol.c.o
[6/9] Compiling C object glmatrix_demo.p/src_glmatrix.c.o
[7/9] Compiling C object glmatrix_demo.p/src_glmatrix_harness.c.o
[8/9] Compiling C object glmatrix_demo.p/src_xscreensaver_compat.c.o
[9/9] Compiling C object glmatrix_demo.p/src_image_data_to_ximage.c.o
[10/N] Linking target wl-screenhack
[11/N] Linking target gl4es_triangle
[12/N] Linking target glmatrix_demo
```

If a compile error appears in `src/glmatrix.c`, it's a shim gap —
report the exact symbol the error names, and add it to
`src/xscreensaver_compat.{h,c}` (don't edit glmatrix.c itself).

If a compile error appears in `src/xscreensaver_compat.c`, that's our
bug — fix it. The compat shim IS our code (vendored glmatrix.c is NOT).

**Run the static analysis check**:

```bash
# -Wall -Wextra must be clean for our code:
meson setup build --wipe -Dwarning_level=2
meson compile -C build 2>&1 | grep -E "warning:|error:" | grep -v "vendored\|xscreensaver" || echo "no warnings in our code"
```

---

## 3. Part 1 verification: GL4ES triangle

```bash
# Run with the CRITICAL env vars:
LIBGL_NOTEST=1 LIBGL_ES=2 LIBGL_NOBANNER=1 ./build/gl4es_triangle 2>&1 | tee /tmp/gl4es_triangle.log
```

Expected on stderr (the diagnostic block inside gl4es_triangle.c):

```
[diag] EGL current: dpy=0x... ctx=0x... srf=0x...
[diag] GL_VENDOR   = ptitSeb
[diag] GL_RENDERER = GL4ES
[diag] GL_VERSION  = 1.4 gl4es ...
[diag] GL_SHADER   = ...
[diag] glGetError() = 0x0
[diag] glBegin/glVertex3f/glEnd issued; glGetError() = 0x0
[diag] gldes_triangle: if you see a 3-color (red/green/blue) triangle ...
```

**WHAT PROVES SUCCESS**:

1. **`GL_VENDOR = ptitSeb`** — if this says "Mesa" or anything else,
   GL4ES is NOT in the link path. Stop. Verify
   `LD_LIBRARY_PATH=/usr/lib/gl4es ldd ./build/gl4es_triangle | grep libGL`
   shows `/usr/lib/gl4es/libGL.so.1`. If yes and the vendor still says
   Mesa, report; this is a bug.

2. **`GL_VERSION` contains "gl4es"** — same check; if it's
   "OpenGL ES 3.2" raw, GL4ES isn't being called.

3. **`glGetError() = 0x0`** after `glEnd()` — no GL errors. If
   non-zero, the GL4ES fixed-function path threw an error. Report.

4. **VISUAL**: the screen shows a rotating rainbow triangle (red top,
   green lower-left, blue lower-right), spinning clockwise around its
   center, ~one revolution per 12 seconds.

**FAILURE MODES** and what they mean:

| Symptom | Meaning | Action |
|---|---|---|
| `LIBGL: Error while gathering supported extension (eglInitialize: EGL_BAD_DISPLAY)` | `LIBGL_NOTEST=1` not set | Set it; rerun |
| `GL_VERSION = OpenGL ES 3.2 Mesa` (no "gl4es") | GL4ES not in link path | Verify `-rpath /usr/lib/gl4es` and `LD_LIBRARY_PATH` |
| `GL_INVALID_ENUM` after glBegin | Same — GL4ES not in path | Same |
| Black screen but no errors | Possibly GL4ES init failed silently | Check `LIBGL_NOTEST`; also check `LIBGL_ES=2` matches `DEFAULT_ES=2` |
| Crash on init | Probably the EGL context-creation order is wrong | The harness binds EGL API, creates ES2 context, makes current; GL4ES then loads. If you see crashes here, file a bug with the exact stack trace. |

---

## 4. Part 2 verification: glmatrix through the pipeline

```bash
LIBGL_NOTEST=1 LIBGL_ES=2 LIBGL_NOBANNER=1 ./build/glmatrix_demo 2>&1 | tee /tmp/glmatrix_demo.log
```

Expected on stderr:

```
[diag] glmatrix_harness: calling init_matrix...
[diag] glmatrix_harness: init_matrix returned
```

…then nothing (no further messages). The hack runs the GL draw path
silently. Glmatrix sets `mi->fps_p = False` so do_fps isn't called.

**WHAT PROVES SUCCESS**:

1. **No "GL error" messages** in the log. glmatrix's
   `check_gl_error("texture init")` and `check_gl_error("texture param")`
   would print `xscreensaver_compat: GL error after <label>: 0xXXX`
   on failure.

2. **VISUAL**: the "Matrix" rain effect — columns of green katakana
   characters (or similar glyphs from the matrix3 PNG) falling top-to-
   bottom over a black background, with the camera slowly rotating
   around the column field. This is the iconic screensaver.

3. **Performance**: at the screen's native resolution, on the
   Mali-G720, the effect should sustain ≥30 FPS. If it drops to
   single-digit FPS, GL4ES's per-vertex shader overhead is the likely
   culprit (file a tuning bug; not blocking for round 7).

4. **Quit behavior**: pressing any key (the wl-screenhack keyboard
   listener quits on KeyPress) cleanly terminates; the process exits
   with status 0; no segfault in the cleanup path.

**FAILURE MODES**:

| Symptom | Meaning | Action |
|---|---|---|
| `xscreensaver_compat: GL error after texture init: 0x500` | GL_INVALID_ENUM — a glTexParameteri call used an enum GL4ES doesn't know | Add to GL4ES's known-enum list (probably already there in HEAD); rerun |
| `xscreensaver_compat: GL error after creating 512x512 texture: 0x500` | glTexImage2D's format/type combo rejected | Check the matrix3 PNG is 512x598 8-bit indexed; libpng should decode to RGBA8; the format combo is standard |
| Black screen but no error | glmatrix's load_textures might be silently failing on texture allocation; or GL4ES's FPE matrix stack is not behaving like real GL1 | Check `LIBGL_NOTEST=1` is set; add `LIBGL_BEGINEND=1 LIBGL_FB=0` to force legacy fixed-function path |
| Segfault in init_matrix | Almost certainly NULL deref somewhere in the shim. The most likely culprits: `mp->glx_context` (we return a sentinel), `mps` allocation (we calloc). Check the gdb backtrace. |

---

## 5. Sanity-check the build artifacts

```bash
# wl-screenhack still builds (rounds 1-6 must not be regressed):
./build/wl-screenhack &
sleep 2
pkill -INT wl-screenhack        # should exit cleanly on SIGINT
wait
echo "wl-screenhack exit OK"

# gl4es_triangle links against GL4ES:
ldd ./build/gl4es_triangle | grep -E "libGL|libEGL|libGLESv2"
# expect:
#   libGL.so.1 => /usr/lib/gl4es/libGL.so.1
#   libEGL.so.1 => /usr/lib/aarch64-linux-gnu/libEGL.so.1
#   libGLESv2.so.2 => /usr/lib/aarch64-linux-gnu/libGLESv2.so.2

# glmatrix_demo also links against GL4ES:
ldd ./build/glmatrix_demo | grep -E "libGL|libpng"
# expect libGL.so.1 => /usr/lib/gl4es/libGL.so.1 AND libpng
```

---

## 6. What to report back

A single text file (or markdown) with:

```
=== ROUND 7 RESULTS ===
Date: YYYY-MM-DD
Hardware: Debian forky arm64, labwc 0.9.5, wlroots 0.20.2,
          Mali-G720-Immortalis / Mesa panfrost (versions from uname/eglinfo)

GL4ES build:    OK / FAIL  (commit pinned to: <hash>)
ncz-screensavers build:  OK / FAIL
  wl-screenhack:          OK / FAIL
  gl4es_triangle:         OK / FAIL
  glmatrix_demo:          OK / FAIL

Part 1 (gl4es_triangle):
  stderr output: <paste>
  visual: <describe — did the rotating triangle render?>
  GL_VENDOR / GL_VERSION reported: <paste from stderr>

Part 2 (glmatrix_demo):
  stderr output: <paste>
  visual: <describe — did the Matrix rain render?>
  performance: <FPS estimate>
  quit behavior: <clean / hung / crashed>

If FAIL: paste the exact error verbatim. Do NOT paraphrase.
```

---

## 7. After successful verification

A future round (round 8+) can:
- Add a `-e <name>` flag to `wl-screenhack` to dispatch into any
  registered effect, unifying the three binaries.
- Vendor the next 5-10 simple GL hacks (`gears`, `boing`, `sproingies`,
  `boxed`, `fireworkx`) through the same recipe.
- Vendor ptitSeb/GLU (~3K LoC, autotools) for the hacks that use
  gluSphere/gluCylinder.
- Add XAllocColor / XParseColor shim functions for hacks that build
  color palettes.

The mechanical pipeline is now in place; scaling to ~200 hacks is
mostly a matter of:
1. Running the per-hack #include-edit + shim-symbol-add recipe.
2. Verifying each on real hardware.
3. Logging the deltas in CHANGELOG.md.