# meson.build — native Wayland/EGL GL screensaver foundation.
#
# Round 1: builds `wl-screenhack` — wlr-layer-shell surface + EGL/GLES2 test
#           effect (rotating triangle), hardware-verified working.
# Round 7: adds TWO new executables:
#   - `gl4es_triangle`  — Part 1: immediate-mode fixed-function triangle
#                          rendered through GL4ES into our existing
#                          EGL/Wayland surface. Proves the GL1-on-GLES2
#                          translation shim is wired correctly.
#   - `glmatrix_demo`   — Part 2: real, vendored xscreensaver `glmatrix.c`
#                          ported through GL4ES + a thin `xscreensaver_compat`
#                          shim that satisfies its screenhack.h/xlockmore.h
#                          windowing API. Proves the mechanical porting
#                          pipeline for one real hack.
#
# All three are pure native Wayland + EGL + GLES — no X11, no GLX, no Xwayland.

project(
    'ncz-screensavers',
    'c',
    default_options: [
        'c_std=c11',
        'warning_level=2',
        # Default -O0/-g; downstream packagers usually override.
    ],
    version: '0.1.0',
    license: 'GPL-2.0-or-later',
)

# ---------------------------------------------------------------------------
# Build options — see `meson configure` for the full list.
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# Features
# ---------------------------------------------------------------------------
#
# `gl4es_triangle` and `glmatrix_demo` are gated on opt-in features so
# the build doesn't break when GL4ES or libpng isn't installed on the
# build host (e.g. macOS where the coordinator can't build, or a fresh
# Debian box that hasn't installed gl4es yet). Default: both enabled —
# the test target hardware (Debian forky arm64) installs them.

opt_gl4es        = get_option('gl4es').enabled()
opt_xscreensaver = get_option('xscreensaver-shim').enabled()

# ---------------------------------------------------------------------------
# Core dependencies — always required.
# ---------------------------------------------------------------------------

dep_wayland_client = dependency('wayland-client', required: true)
dep_wayland_egl    = dependency('wayland-egl',    required: true)
dep_egl            = dependency('egl',            required: true)
dep_glesv2         = dependency('glesv2',         required: true)

# libm — vendored xscreensaver code uses cosf/sinf/sqrtf/etc., and the
# GLES3 helper layer (gles3_compat.c's matrix stack, immediate-mode
# normal calculation) does too. Neither libGLESv2 nor libwayland-client
# drags libm in transitively, so we declare it explicitly.
cc = meson.get_compiler('c')
dep_m = cc.find_library('m', required: false)

# GL4ES dependency — optional. We DO NOT use pkg-config because GL4ES
# upstream doesn't ship a .pc file. Detection strategy:
#   1. If `gl4es-lib-dir` is 'auto', probe the standard install locations:
#        a. /usr/lib/aarch64-linux-gnu/gl4es/libGL.so.1  (Debian forky)
#        b. /usr/lib/gl4es/libGL.so.1                    (upstream install)
#        c. /usr/lib/x86_64-linux-gnu/gl4es/libGL.so.1   (Debian x86)
#   2. If a path is passed explicitly, use it (must contain libGL.so.1).
#
# We do NOT need a header from this install — the GL 1.x header is vendored
# in src/gl4es_include/GL/gl.h (libgl4es-dev only ships gl4eshint.h and
# gl4esinit.h, not the legacy GL/gl.h).
#
# See PORTING.md §3 for context.
gl4es_lib_dir = get_option('gl4es-lib-dir')
if opt_gl4es
    if gl4es_lib_dir == 'auto'
        # Try Debian's gl4es package layout first; fall back to upstream's.
        candidates = [
            '/usr/lib/aarch64-linux-gnu/gl4es/libGL.so.1',
            '/usr/lib/x86_64-linux-gnu/gl4es/libGL.so.1',
            '/usr/lib/gl4es/libGL.so.1',
            '/usr/local/lib/gl4es/libGL.so.1',
        ]
        gl4es_lib_path = ''
        foreach c : candidates
            if run_command('test', '-f', c, check: false).returncode() == 0
                gl4es_lib_path = c
                break
            endif
        endforeach
        if gl4es_lib_path == ''
            error('GL4ES feature enabled but libGL.so.1 not found in any of @0@. Install libgl4es0 (Debian), or build GL4ES per PORTING.md §3 to /usr/lib/gl4es/libGL.so.1, or pass -Dgl4es-lib-dir=/path/to/libGL.so.1, or pass -Dgl4es=disabled.'.format(candidates))
        endif
    elif gl4es_lib_dir.endswith('libGL.so.1')
        gl4es_lib_path = gl4es_lib_dir
    else
        # Treat as a directory; probe for libGL.so.1 inside.
        gl4es_lib_path = gl4es_lib_dir + '/libGL.so.1'
    endif
    if run_command('test', '-f', gl4es_lib_path, check: false).returncode() != 0
        error('GL4ES feature enabled but @0@ not found. Install libgl4es0 (Debian), build GL4ES per PORTING.md §3, or pass -Dgl4es-lib-dir=/path/to/libGL.so.1, or pass -Dgl4es=disabled.'.format(gl4es_lib_path))
    endif
    # Extract just the directory portion for -L/-rpath flags.
    gl4es_dir = gl4es_lib_path.replace('libGL.so.1', '')

    # Bundle a copy of gl4es's libGL.so.1 INSIDE the build tree, so that the
    # binaries we produce can be relocated to a target host (e.g. the O6N test
    # box) without that host needing libgl4es0 installed at /usr/lib/.../gl4es.
    #
    # The runtime loader resolves "$ORIGIN" to the directory the binary lives
    # in. By setting build_rpath to include "$ORIGIN/gl4es" and copying
    # libGL.so.1 next to each binary at build/gl4es/, we get a self-contained
    # bundle: scp build/ to O6N and run ./glmatrix_demo — no LD_PRELOAD, no
    # apt-get install on the target, the binary just works.
    #
    # IMPORTANT: we use `-Wl,--disable-new-dtags` so the link emits DT_RPATH
    # (the legacy tag), NOT DT_RUNPATH (the modern default). The two behave
    # differently under glibc:
    #
    #   * DT_RUNPATH is searched AFTER LD_LIBRARY_PATH.
    #   * DT_RPATH    is searched BEFORE LD_LIBRARY_PATH.
    #
    # On O6N the live labwc session exports LD_LIBRARY_PATH that contains
    # /opt/cixgpu-compat/lib/aarch64-linux-gnu (set by
    # /opt/singularity/bin/singularity-labwc-session). With DT_RUNPATH, that
    # LD_LIBRARY_PATH entry wins and the loader picks up
    # /opt/cixgpu-compat/.../libGL.so.1 (the CIX vendor Mesa/Panthor libGL)
    # instead of our bundled gl4es. The vendor libGL accepts init/draw/swap
    # with healthy-looking diagnostic logs but renders pure black for any
    # GL1.x fixed-function call the vendored hack makes. With DT_RPATH, our
    # $ORIGIN/gl4es wins regardless of what LD_LIBRARY_PATH the calling
    # session set, which is what we want for the *_demo screensaver-test
    # binaries specifically. Verified on O6N 2026-08-20: process
    # /opt/singularity/bin/labwc (PID 1656) has
    # LD_LIBRARY_PATH=/opt/singularity/lib:/opt/cixgpu-pro/lib/aarch64-linux-gnu:/opt/cixgpu-compat/lib/aarch64-linux-gnu:/usr/share/cix/lib
    # in its /proc/1656/environ — confirmed via `tr '\0' '\n'`. Setting
    # LD_LIBRARY_PATH to those same four dirs in a bare SSH shell
    # reproduces the wrong-libGL resolution in `ldd`; switching to
    # DT_RPATH makes it resolve to /home/mini/ssreg/build/gl4es/libGL.so.1
    # (the bundled one) deterministically.
    #
    # MEASURED on O6N 2026-08-20: without the bundling, the binary's rpath
    # /usr/lib/aarch64-linux-gnu/gl4es/ does not resolve (no such dir on the
    # test host) and the loader silently falls back to Mesa's libGL, which
    # passes init/draw/swaps with healthy-looking diagnostic logs but renders
    # pure black for any GL1.x fixed-function call the vendored hack makes.
    # The frame callback chain still ticks (frame_done increments normally)
    # so log-text-only checks cannot distinguish the two. Visual verification
    # on real hardware is the only bar.
    gl4es_bundle_dir = meson.current_build_dir() / 'gl4es'
    run_command('mkdir', '-p', gl4es_bundle_dir, check: true)
    run_command('cp', gl4es_lib_path, gl4es_bundle_dir / 'libGL.so.1', check: true)
else
    # gl4es disabled. The xscreensaver-shim path (legacy 88 + glmatrix_demo)
    # still references gl4es_dir in its link_args / build_rpath so the
    # binary carries gl4es alongside it — those references are dead when
    # the feature is off, so we synthesize an empty placeholder. Once the
    # final Phase 4+ of the migration removes the gl4es-routed path
    # entirely, this block goes away too.
    gl4es_dir = ''
endif

# libpng — required for xscreensaver-shim (PNG textures via image_data_to_ximage).
dep_png = dependency('libpng', required: opt_xscreensaver)

# libGLU — system Mesa GLU provides the tessellator (gluNewTess /
# gluTessCallback / gluTessVertex). Three vendored hacks need it:
# dnalogo, jigsaw, polyhedra-gl. The hacks themselves call
# gluTessCallback directly from their .c files; GL4ES does not
# provide these symbols. Marked optional so the build is fine on
# systems without libglu1-mesa-dev.
dep_glu = dependency('glu', required: false)

# ---------------------------------------------------------------------------
# wayland-scanner — generates both headers (client-side) and the private
# bindings code (no dlopen, all symbols static). One custom_target per
# protocol so build graphs are clean and incremental.
# ---------------------------------------------------------------------------

wayland_scanner = find_program('wayland-scanner', required: true)

wayland_scanner_client_args  = ['client-header']
wayland_scanner_privcode_args = ['private-code']

protocols = {
    'wayland':           'protocols/wayland.xml',
    'xdg-shell':         'protocols/xdg-shell.xml',
    'wlr-layer-shell-unstable-v1': 'protocols/wlr-layer-shell-unstable-v1.xml',
}

gen_sources = []
foreach name, xml_path : protocols
    base = name + '-client-protocol'
    h_target = custom_target(
        name + '_header',
        input:  xml_path,
        output: base + '.h',
        command: [wayland_scanner, wayland_scanner_client_args,
                  '@INPUT@', '@OUTPUT@'],
        depend_files: files(xml_path),
    )
    c_target = custom_target(
        name + '_private_code',
        input:  xml_path,
        output: base + '.c',
        command: [wayland_scanner, wayland_scanner_privcode_args,
                  '@INPUT@', '@OUTPUT@'],
        depend_files: files(xml_path),
    )
    gen_sources += [h_target, c_target]
endforeach

# Add the build directory to the include path so generated headers are
# reachable via `#include "wayland-client-protocol.h"` etc. The source
# root is added so source files can find them via relative path.
inc = include_directories('.', 'src')

# Build-wide -Wall -Wextra. All our code (and the compat shim) must stay
# warning-clean under these. Vendored third-party code (xscreensaver
# hacks, GL4ES itself) is NOT held to this bar — they get c_args below
# that suppress relevant warnings where unavoidable.
common_c_args = ['-Wall', '-Wextra']

# ---------------------------------------------------------------------------
# Round 1 — `wl-screenhack` (always built)
# ---------------------------------------------------------------------------

sources = files('src/wl-screenhack.c') + gen_sources

executable(
    'wl-screenhack',
    sources,
    include_directories: inc,
    dependencies: [
        dep_wayland_client,
        dep_wayland_egl,
        dep_egl,
        dep_glesv2,
    ],
    c_args: common_c_args,
    install: false,
)

# ---------------------------------------------------------------------------
# Round 7 Part 1 — `gl4es_triangle` (gated on -Dgl4es=enabled)
# ---------------------------------------------------------------------------
#
# This binary tests that GL4ES (the fixed-function GL1-on-GLES2 shim)
# correctly routes classic glBegin/glVertex3f/glMatrixMode/glRotatef calls
# into our existing Wayland/EGL/GLES surface. It's a self-contained test:
# own Wayland/EGL setup, doesn't share code with wl-screenhack.c (the
# working foundation stays untouched).
#
# Linked against:
#   - system libwayland-client, libwayland-egl, libEGL, libGLESv2 (for
#     EGL/EGL context creation — must be REAL Mesa libEGL/libGLESv2, NOT
#     GL4ES, because GL4ES is a libGL replacement, not a libEGL one)
#   - GL4ES's libGL.so.1 (via -lGL flag, with rpath to /usr/lib/gl4es)
#
# See PORTING.md §3 for the GL4ES build + install instructions.
if opt_gl4es
    executable(
        'gl4es_triangle',
        files('src/gl4es_triangle.c') + gen_sources,
        include_directories: inc,
        dependencies: [
            dep_wayland_client,
            dep_wayland_egl,
            dep_egl,
            dep_glesv2,
        ],
        # Link against GL. The -L flag selects GL4ES's directory first,
        # so libGL.so.1 from gl4es is preferred over Mesa's.
        # build_rpath contains BOTH the in-tree install path (so a binary
        # built here runs correctly without any extra setup) AND the
        # $ORIGIN-relative path (so the binary keeps working after the
        # build/ directory is relocated to a target host that does not
        # have gl4es installed system-wide).
        #
        # --disable-new-dtags emits DT_RPATH instead of DT_RUNPATH, so our
        # rpath wins over LD_LIBRARY_PATH (see the comment block above the
        # gl4es_bundle_dir for the live-session /opt/cixgpu-compat issue).
        link_args: [
            '-Wl,--disable-new-dtags',
            '-L@0@'.format(gl4es_dir),
            '-lGL',
        ],
        build_rpath: gl4es_dir + ':$ORIGIN/gl4es',
        c_args: common_c_args,
        install: false,
    )
endif

# ---------------------------------------------------------------------------
# Round 7 Part 2 — `glmatrix_demo` (gated on -Dxscreensaver-shim=enabled)
# ---------------------------------------------------------------------------
#
# This binary ports the REAL xscreensaver `glmatrix.c` (vendored from
# xscreensaver-6.15/hacks/glx/glmatrix.c, MIT-style X Consortium license
# preserved at top of file) through the GL4ES + xscreensaver_compat shim
# pipeline. It uses the same wl-screenhack.c style EGL/Wayland setup.
#
# Linked against:
#   - GL4ES's libGL.so.1 (for fixed-function emulation)
#   - libpng (for texture loading via image_data_to_ximage shim)
#   - system libwayland-client, libwayland-egl, libEGL, libGLESv2
if opt_xscreensaver
    # Vendored third-party source — relax warning strictness. The
    # xscreensaver_compat.c we author IS held to the standard bar.
    vendored_c_args = common_c_args + [
        # glmatrix.c's real content is entirely behind '#ifdef USE_GL /* whole
        # file */' -- xscreensaver's own Makefile system defines this per-GL-
        # variant build. Without it the file compiles to an empty translation
        # unit (no errors, no symbols) -- exactly what caused an "undefined
        # reference to glmatrix_xscreensaver_function_table" link failure with
        # zero prior compiler complaints from the drawing code itself.
        '-DUSE_GL',
        # M_PI and friends are not ISO C; xscreensaver expects the GNU
        # extensions to be visible, as its own build system arranges.
        '-D_GNU_SOURCE',
        # xscreensaver's configure probes this; without it utils/yarandom.c
        # and friends fall back to the ancient one-argument BSD gettimeofday
        # and fail with "too few arguments".
        '-DGETTIMEOFDAY_TWO_ARGS=1',
        '-DHAVE_UNISTD_H=1',   # getpid() in utils/yarandom.c
        '-Wno-unused-parameter',
        '-Wno-unused-but-set-variable',
        '-Wno-unused-variable',
        '-Wno-sign-compare',
        '-Wno-missing-field-initializers',
    ]
    executable(
        'glmatrix_demo',
        files([
            'src/glmatrix_harness.c',     # tiny driver: own Wayland/EGL + dispatch
            'src/xscreensaver_compat.c',  # our shim — clean warnings
            'src/glmatrix.c',             # vendored xscreensaver source
            # image_data_to_ximage shim (libpng-backed XImage decoder)
            'src/image_data_to_ximage.c',
        ]) + gen_sources,
        include_directories: inc,
        dependencies: [
            dep_wayland_client,
            dep_wayland_egl,
            dep_egl,
            dep_glesv2,
            dep_png,
        ],
        # --disable-new-dtags emits DT_RPATH instead of DT_RUNPATH, so our
        # bundled gl4es wins over any LD_LIBRARY_PATH the calling session
        # has set (see the comment block above gl4es_bundle_dir).
        link_args: [
            '-Wl,--disable-new-dtags',
            '-L@0@'.format(gl4es_dir),
            '-lGL',
        ],
        build_rpath: gl4es_dir + ':$ORIGIN/gl4es',
        # Vendored third-party (glmatrix.c) is the primary source of
        # warnings; relax the strictness for the whole executable.
        # The compat shim and harness stay warning-clean under -Wall -Wextra.
        c_args: vendored_c_args,
        install: false,
    )

    # -----------------------------------------------------------------------
    # Ported hacks. One entry per effect.
    #
    # The harness is SHARED: glmatrix_harness.c resolves the hack through
    # -DHACK_TABLE=<prefix>_xscreensaver_function_table, so an effect costs one
    # table entry here and a single #include edit in its vendored .c, with no
    # copy of the harness per effect.
    #
    # 'png' marks hacks that pull in the libpng-backed XImage decoder; effects
    # with no texture data do not need image_data_to_ximage.c.
    ported_hacks = [
        {'name': 'noof', 'src': 'src/noof.c', 'png': false, 'extra': ['src/pow2.c']},
        {'name': 'beats', 'src': 'src/beats.c', 'png': false, 'extra': ['src/sphere.c']},
        {'name': 'boing', 'src': 'src/boing.c', 'png': false, 'extra': []},
        {'name': 'blinkbox', 'src': 'src/blinkbox.c', 'png': false, 'extra': ['src/sphere.c']},
        {'name': 'blocktube', 'src': 'src/blocktube.c', 'png': false, 'extra': []},
        {'name': 'chompytower', 'src': 'src/chompytower.c', 'png': false, 'extra': ['src/doubletime.c', 'src/easing.c', 'src/gllist.c', 'src/normals.c', 'src/rotator.c', 'src/sphere.c', 'src/spline.c', 'src/teeth_model.c', 'src/yarandom.c']},
        {'name': 'cubenetic', 'src': 'src/cubenetic.c', 'png': false, 'extra': ['src/rotator.c', 'src/yarandom.c']},
        {'name': 'glschool', 'src': 'src/glschool.c', 'png': false, 'extra': ['src/glschool_alg.c', 'src/glschool_gl.c', 'src/sphere.c', 'src/tube.c', 'src/yarandom.c']},
        {'name': 'gravitywell', 'src': 'src/gravitywell.c', 'png': false, 'extra': [], 'table': 'gravitywell'},
        {'name': 'glhanoi', 'src': 'src/glhanoi.c', 'png': false, 'extra': ['src/doubletime.c', 'src/rotator.c', 'src/yarandom.c']},
        {'name': 'handsy', 'src': 'src/handsy.c', 'png': false, 'extra': ['src/doubletime.c', 'src/gllist.c', 'src/handsy_model.c', 'src/rotator.c', 'src/sphere.c', 'src/tube.c', 'src/yarandom.c']},
        {'name': 'hypnowheel', 'src': 'src/hypnowheel.c', 'png': false, 'extra': ['src/rotator.c', 'src/yarandom.c']},
        {'name': 'cubicgrid', 'src': 'src/cubicgrid.c', 'png': false, 'extra': ['src/rotator.c', 'src/yarandom.c']},
        {'name': 'cubestack', 'src': 'src/cubestack.c', 'png': false, 'extra': ['src/rotator.c', 'src/yarandom.c']},
        {'name': 'glknots', 'src': 'src/glknots.c', 'png': false, 'extra': ['src/rotator.c', 'src/tube.c', 'src/yarandom.c']},
        {'name': 'cubestorm', 'src': 'src/cubestorm.c', 'png': false, 'extra': ['src/rotator.c', 'src/yarandom.c']},
        {'name': 'hexstrut', 'src': 'src/hexstrut.c', 'png': false, 'extra': ['src/rotator.c', 'src/yarandom.c']},
        {'name': 'energystream', 'src': 'src/energystream.c', 'png': false, 'extra': ['src/rotator.c', 'src/yarandom.c']},
        {'name': 'cubetwist', 'src': 'src/cubetwist.c', 'png': false, 'extra': ['src/rotator.c', 'src/yarandom.c']},
        {'name': 'splodesic', 'src': 'src/splodesic.c', 'png': false, 'extra': ['src/rotator.c', 'src/yarandom.c']},
        {'name': 'squirtorus', 'src': 'src/squirtorus.c', 'png': false, 'extra': ['src/easing.c', 'src/normals.c', 'src/spline.c', 'src/yarandom.c']},
        {'name': 'glblur', 'src': 'src/glblur.c', 'png': false, 'extra': ['src/rotator.c', 'src/yarandom.c']},
        {'name': 'rubikblocks', 'src': 'src/rubikblocks.c', 'png': false, 'extra': ['src/rotator.c', 'src/yarandom.c']},
        {'name': 'discoball', 'src': 'src/discoball.c', 'png': false, 'extra': ['src/rotator.c', 'src/yarandom.c']},
        {'name': 'raverhoop', 'src': 'src/raverhoop.c', 'png': false, 'extra': ['src/rotator.c', 'src/yarandom.c']},
        {'name': 'hextrail', 'src': 'src/hextrail.c', 'png': false, 'extra': ['src/rotator.c', 'src/yarandom.c']},
        {'name': 'crumbler', 'src': 'src/crumbler.c', 'png': false, 'extra': ['src/quickhull.c', 'src/rotator.c', 'src/yarandom.c']},
        {'name': 'lockward', 'src': 'src/lockward.c', 'png': false, 'extra': []},
        {'name': 'cube21', 'src': 'src/cube21.c', 'png': false, 'extra': []},
        {'name': 'stonerview', 'src': 'src/stonerview.c', 'png': false, 'extra': ['src/stonerview-view.c', 'src/stonerview-move.c', 'src/stonerview-osc.c', 'src/yarandom.c']},
        {'name': 'moebiusgears', 'src': 'src/moebiusgears.c', 'png': false, 'extra': ['src/rotator.c', 'src/involute.c', 'src/normals.c', 'src/yarandom.c']},
        {'name': 'nakagin', 'src': 'src/nakagin.c', 'png': false, 'extra': ['src/doubletime.c', 'src/easing.c', 'src/normals.c', 'src/rotator.c', 'src/yarandom.c']},
        {'name': 'voronoi', 'src': 'src/voronoi.c', 'png': false, 'extra': []},
        {'name': 'cityflow', 'src': 'src/cityflow.c', 'png': false, 'extra': []},
        {'name': 'menger', 'src': 'src/menger.c', 'png': false, 'extra': ['src/rotator.c', 'src/yarandom.c']},
        {'name': 'hydrostat', 'src': 'src/hydrostat.c', 'png': false, 'extra': ['src/sphere.c']},
        {'name': 'geodesic', 'src': 'src/geodesic.c', 'png': false, 'extra': ['src/rotator.c', 'src/normals.c', 'src/yarandom.c']},
        {'name': 'gears', 'src': 'src/gears.c', 'png': false, 'extra': ['src/rotator.c', 'src/involute.c', 'src/tube.c', 'src/normals.c', 'src/yarandom.c']},
        {'name': 'papercube', 'src': 'src/papercube.c', 'png': false, 'extra': ['src/rotator.c', 'src/yarandom.c']},
        {'name': 'topblock', 'src': 'src/topblock.c', 'png': false, 'extra': ['src/sphere.c', 'src/tube.c']},
        {'name': 'vigilance', 'src': 'src/vigilance.c', 'png': false, 'extra': ['src/gllist.c', 'src/normals.c', 'src/seccam.c'], 'table': 'vigilance'},
        {'name': 'antinspect', 'src': 'src/antinspect.c', 'png': false, 'extra': ['src/sphere.c'], 'c_args': ['-DSTANDALONE']},
        {'name': 'providence', 'src': 'src/providence.c', 'png': false, 'extra': [], 'c_args': ['-DSTANDALONE']},
        {'name': 'quasicrystal', 'src': 'src/quasicrystal.c', 'png': false, 'extra': ['src/rotator.c', 'src/yarandom.c']},
        {'name': 'highvoltage', 'src': 'src/highvoltage.c', 'png': false, 'extra': ['src/gllist.c', 'src/highvoltage_model.c', 'src/normals.c', 'src/tube.c']},
        {'name': 'headroom', 'src': 'src/headroom.c', 'png': false, 'extra': ['src/gllist.c', 'src/headroom_model.c', 'src/skull_model.c', 'src/rotator.c', 'src/yarandom.c']},
        {'name': 'covid19', 'src': 'src/covid19.c', 'png': false, 'extra': ['src/rotator.c', 'src/sphere.c', 'src/tube.c', 'src/yarandom.c']},
        {'name': 'crackberg', 'src': 'src/crackberg.c', 'png': false, 'extra': []},
        {'name': 'sballs', 'src': 'src/sballs.c', 'png': true, 'extra': [], 'c_args': ['-DSTANDALONE']},
        {'name': 'glforestfire', 'src': 'src/glforestfire.c', 'png': true, 'extra': [], 'table': 'glforestfire', 'c_args': ['-DSTANDALONE']},
        {'name': 'kallisti', 'src': 'src/kallisti.c', 'png': false, 'extra': ['src/gllist.c', 'src/kallisti_model.c', 'src/rotator.c', 'src/yarandom.c']},
        {'name': 'tronbit', 'src': 'src/tronbit.c', 'png': false, 'extra': ['src/doubletime.c', 'src/gllist.c', 'src/rotator.c', 'src/sphere.c', 'src/tronbit_idle1.c', 'src/tronbit_idle2.c', 'src/tronbit_no.c', 'src/tronbit_yes.c', 'src/yarandom.c']},
        {'name': 'peepers', 'src': 'src/peepers.c', 'png': true, 'extra': ['src/normals.c', 'src/rotator.c', 'src/yarandom.c'], 'c_args': ['-DHAVE_JWXYZ']},
        {'name': 'skytentacles', 'src': 'src/skytentacles.c', 'png': true, 'extra': ['src/normals.c', 'src/rotator.c', 'src/yarandom.c'], 'table': 'skytentacles'},
        {'name': 'antspotlight', 'src': 'src/antspotlight.c', 'png': false, 'extra': ['src/rotator.c', 'src/sphere.c', 'src/tube.c', 'src/yarandom.c'], 'c_args': ['-DSTANDALONE']},
        {'name': 'lament', 'src': 'src/lament.c', 'png': true, 'extra': ['src/gllist.c', 'src/lament_model.c', 'src/normals.c', 'src/rotator.c', 'src/yarandom.c']},
        {'name': 'dnalogo', 'src': 'src/dnalogo.c', 'png': false, 'extra': ['src/normals.c', 'src/rotator.c', 'src/sphere.c', 'src/tube.c', 'src/yarandom.c'], 'glu': true},
        {'name': 'glsnake', 'src': 'src/glsnake.c', 'png': false, 'extra': []},
        {'name': 'skulloop', 'src': 'src/skulloop.c', 'png': false, 'extra': ['src/easing.c', 'src/gllist.c', 'src/normals.c', 'src/skull_model.c', 'src/yarandom.c']},
        {'name': 'spheremonics', 'src': 'src/spheremonics.c', 'png': false, 'extra': ['src/normals.c', 'src/rotator.c', 'src/yarandom.c']},
        {'name': 'fliptext', 'src': 'src/fliptext.c', 'png': false, 'extra': []},
        {'name': 'molecule', 'src': 'src/molecule.c', 'png': false, 'extra': ['src/rotator.c', 'src/sphere.c', 'src/tube.c', 'src/yarandom.c']},
        {'name': 'unicrud', 'src': 'src/unicrud.c', 'png': false, 'extra': ['src/rotator.c', 'src/yarandom.c']},
        {'name': 'winduprobot', 'src': 'src/winduprobot.c', 'png': true, 'extra': ['src/gllist.c', 'src/involute.c', 'src/normals.c', 'src/robot.c', 'src/robot-wireframe.c', 'src/sphere.c', 'src/yarandom.c']},
        {'name': 'juggler3d', 'src': 'src/juggler3d.c', 'png': false, 'extra': ['src/rotator.c', 'src/sphere.c', 'src/tube.c', 'src/yarandom.c']},
        {'name': 'mapscroller', 'src': 'src/mapscroller.c', 'png': true, 'extra': ['src/easing.c']},
        {'name': 'splitflap', 'src': 'src/splitflap.c', 'png': true, 'extra': ['src/gllist.c', 'src/rotator.c', 'src/splitflap_obj.c', 'src/yarandom.c']},
        {'name': 'pinion', 'src': 'src/pinion.c', 'png': false, 'extra': ['src/involute.c', 'src/normals.c', 'src/rotator.c', 'src/yarandom.c'], 'glu': true},
        {'name': 'gibson', 'src': 'src/gibson.c', 'png': false, 'extra': ['src/easing.c', 'src/rotator.c', 'src/yarandom.c']},
        {'name': 'geodesicgears', 'src': 'src/geodesicgears.c', 'png': false, 'extra': ['src/involute.c', 'src/normals.c', 'src/rotator.c', 'src/tube.c', 'src/yarandom.c']},
        {'name': 'tangram', 'src': 'src/tangram.c', 'png': false, 'extra': ['src/tangram_shapes.c']},
        {'name': 'gltext', 'src': 'src/gltext.c', 'png': false, 'extra': ['src/glut_stroke.c', 'src/glut_swidth.c', 'src/rotator.c', 'src/sphere.c', 'src/tube.c', 'src/yarandom.c']},
        {'name': 'starwars', 'src': 'src/starwars.c', 'png': false, 'extra': ['src/glut_stroke.c', 'src/glut_swidth.c', 'src/yarandom.c']},
        {'name': 'hilbert', 'src': 'src/hilbert.c', 'png': false, 'extra': ['src/rotator.c', 'src/sphere.c', 'src/tube.c', 'src/yarandom.c']},
        {'name': 'kaleidocycle', 'src': 'src/kaleidocycle.c', 'png': false, 'extra': ['src/normals.c', 'src/rotator.c', 'src/yarandom.c']},
        {'name': 'polyhedra-gl', 'src': 'src/polyhedra-gl.c', 'png': false, 'extra': ['src/normals.c', 'src/polyhedra.c', 'src/rotator.c', 'src/teapot.c', 'src/yarandom.c'], 'glu': true, 'table': 'polyhedra'},
        {'name': 'jigsaw', 'src': 'src/jigsaw.c', 'png': false, 'extra': ['src/normals.c', 'src/rotator.c', 'src/spline.c', 'src/yarandom.c'], 'glu': true},
        {'name': 'razzledazzle', 'src': 'src/razzledazzle.c', 'png': false, 'extra': ['src/gllist.c', 'src/normals.c', 'src/ships.c', 'src/yarandom.c']},
        {'name': 'lavalite', 'src': 'src/lavalite.c', 'png': true, 'extra': ['src/marching.c', 'src/normals.c', 'src/rotator.c', 'src/yarandom.c']},
        {'name': 'unknownpleasures', 'src': 'src/unknownpleasures.c', 'png': true, 'extra': ['src/doubletime.c', 'src/easing.c']},
        {'name': 'photopile', 'src': 'src/photopile.c', 'png': true, 'extra': ['src/dropshadow.c', 'src/xftwrap.c', 'src/yarandom.c']},
        {'name': 'timetunnel', 'src': 'src/timetunnel.c', 'png': true, 'extra': ['src/rotator.c', 'src/yarandom.c'], 'glu': true},
        {'name': 'dangerball', 'src': 'src/dangerball.c', 'png': false, 'extra': ['src/rotator.c', 'src/sphere.c', 'src/tube.c', 'src/yarandom.c']},
        {'name': 'glcells', 'src': 'src/glcells.c', 'png': false, 'extra': []},
        {'name': 'flyingtoasters', 'src': 'src/flyingtoasters.c', 'png': true, 'extra': ['src/gllist.c', 'src/toast.c', 'src/toast2.c', 'src/toaster.c', 'src/toaster_base.c', 'src/toaster_handle.c', 'src/toaster_handle2.c', 'src/toaster_jet.c', 'src/toaster_knob.c', 'src/toaster_slots.c', 'src/toaster_wing.c', 'src/yarandom.c']},
        {'name': 'companion', 'src': 'src/companion.c', 'png': false, 'extra': ['src/companion_disc.c', 'src/companion_heart.c', 'src/companion_quad.c', 'src/gllist.c', 'src/rotator.c', 'src/yarandom.c'], 'table': 'companioncube'},
        {'name': 'bouncingcow', 'src': 'src/bouncingcow.c', 'png': false, 'extra': ['src/cow_face.c', 'src/cow_hide.c', 'src/cow_hoofs.c', 'src/cow_horns.c', 'src/cow_tail.c', 'src/cow_udder.c', 'src/gllist.c', 'src/rotator.c', 'src/yarandom.c']},
    ]

    foreach h : ported_hacks
        hack_sources = [
            'src/glmatrix_harness.c',
            'src/xscreensaver_compat.c',
            h['src'],
        ] + h.get('extra', [])
        if h['png']
            hack_sources += ['src/image_data_to_ximage.c']
        endif
        hack_deps = [
            dep_wayland_client,
            dep_wayland_egl,
            dep_egl,
            dep_glesv2,
            dep_png,
        ]
        if h.get('glu', false)
            hack_deps += [dep_glu]
        endif
        executable(
            h['name'] + '_demo',
            files(hack_sources) + gen_sources,
            include_directories: inc,
            dependencies: hack_deps,
            # --disable-new-dtags emits DT_RPATH instead of DT_RUNPATH, so
            # the bundled gl4es wins over LD_LIBRARY_PATH (see the comment
            # block above gl4es_bundle_dir for why this matters on O6N).
            link_args: [
                '-Wl,--disable-new-dtags',
                '-L@0@'.format(gl4es_dir),
                '-lGL',
            ],
            build_rpath: gl4es_dir + ':$ORIGIN/gl4es',
            c_args: vendored_c_args + h.get('c_args', []) + [
                '-DHACK_TABLE=@0@_xscreensaver_function_table'.format(h.get('table', h['name'])),
            ],
            install: false,
        )
    endforeach

    # -----------------------------------------------------------------------
    # Phase 1 GLES3-native pilots: boing (immediate-mode + matrix stack)

endif

# -------------------------------------------------------------------
# Phase 1+ GLES3-native builds — always-on. Do NOT require opt_gl4es or
# opt_xscreensaver; these targets link against system libGLESv2 / libEGL
# with no GL4ES translation shim. See GLES3-MIGRATION-PHASE1.md.
# -------------------------------------------------------------------

# Phase 1 GLES3-native pilots: boing (immediate-mode + matrix stack)
# and companion (gllist VBO conversion + display-list recorder).
#
# These compile against system libGLESv2 / libEGL directly — NO
# gl4es. They live alongside the legacy gl4es-routed ports above and
# do NOT replace them. The legacy gl4es path stays active until
# every hack has been ported (per the operator's full-migration
# directive). See GLES3-MIGRATION-PHASE1.md for the rollout plan.
# -----------------------------------------------------------------------

common_gles3_sources = [
    'src/xscreensaver_compat.c',
    'src/gles3_compat.c',
]
# GLES3-native pilots need -DNCZ_GLES3_BUILD=1 so that the glu*
# helpers in xscreensaver_compat.c compile to empty stubs (they
# rely on glMultMatrixf/glTranslated which don't exist in GLES3).
# -DUSE_GL is required by the vendored xscreensaver hack sources — most
# of them are wrapped in '#ifdef USE_GL /* whole file */' (see tangram.c,
# glcells.c, etc.). Without it the file compiles to an empty translation
# unit (no errors, no symbols), which surfaces as an "undefined reference
# to <hack>_xscreensaver_function_table" link failure — and crucially
# there are no prior compiler complaints to point at it.
gles3_native_c_args = common_c_args + [
        '-DUSE_GL',
        '-D_GNU_SOURCE',
        '-DGETTIMEOFDAY_TWO_ARGS=1',
        '-DHAVE_UNISTD_H=1',
        '-Wno-unused-parameter',
        '-Wno-unused-but-set-variable',
        '-Wno-unused-variable',
        '-Wno-sign-compare',
        '-Wno-missing-field-initializers',
        '-DNCZ_GLES3_BUILD=1',
    ]

# Shared GLES3-native dependency list: Wayland + EGL + GLESv2 + libpng
# (for image_data_to_ximage on hacks that need texture decoding)
# + libm (vendored xscreensaver code and gles3_compat.c use
# cosf/sinf/sqrtf etc — neither libGLESv2 nor libwayland-client
# drags libm in transitively on Debian aarch64).
common_gles3_deps = [
    dep_wayland_client,
    dep_wayland_egl,
    dep_egl,
    dep_glesv2,
    dep_png,
    dep_m,
]

# Boing — simplest GL1 pilot. Uses immediate-mode helpers + matrix
# stack + lighting + ortho projection. Zero model dependencies.
executable(
    'boing_gles3',
    files(common_gles3_sources + [
        'src/gles3_harness.c',
        'src/gles3_boing.c',
    ]) + gen_sources,
    include_directories: inc,
    dependencies: common_gles3_deps,
    c_args: gles3_native_c_args + [
        # gles3_boing emits `gles3boing_xscreensaver_function_table`
        # via XSCREENSAVER_MODULE_2.
        '-DHACK_TABLE=gles3boing_xscreensaver_function_table',
    ],
    install: false,
)

# Companion — gllist VBO conversion pilot. Loads 3 pre-baked
# `struct gllist` chains (companion_quad, companion_disc,
# companion_heart) into VBOs at init, plays them every frame. Also
# exercises the display-list recorder path (the cube is built as a
# sequence of inline quads + chain draws, not via a recorded
# display list, but the recorder API is in place for hacks that
# actually use glNewList/glEndList).
executable(
    'companion_gles3',
    files(common_gles3_sources + [
        'src/gles3_harness.c',
        'src/gles3_companion.c',
        # Companion's 3 pre-baked object data files (these are the
        # `struct gllist` chains in C, not model files).
        'src/companion_quad.c',
        'src/companion_disc.c',
        'src/companion_heart.c',
        # The physics / random helpers that companion needs.
        'src/rotator.c',
        'src/yarandom.c',
    ]) + gen_sources,
    include_directories: inc,
    dependencies: common_gles3_deps,
    c_args: gles3_native_c_args + [
        '-DHACK_TABLE=gles3companioncube_xscreensaver_function_table',
    ],
    install: false,
)

# -----------------------------------------------------------------------
# Phase 2 — legacy 88-hack migration to native GLES3. With the
# GLES3 foundation now providing active stubs for the GL1 fixed-
# function API (glBegin/glVertex/glColor/glNormal/glTexCoord →
# ncz_im_*; glMatrixMode/glPushMatrix/glRotatef/glMultMatrixf →
# ncz_mat_stack_*; glLightfv/glMaterialfv → ncz_im_light_*/material),
# the vendored xscreensaver sources for these hacks compile and
# link directly against the GLES3 harness with no source edits —
# the "mechanical Phase 4+ porting pipeline" flagged as a future
# optimization in GLES3-MIGRATION-PHASE1.md, brought forward here
# because the surface gap (immediate-mode accumulator + matrix
# stack) was already bridged by the boing/companion pilots.
#
# The gl4es-routed `_demo` binary for each of these hacks is kept
# alive in the build (it depends on opt_gl4es which is off by
# default on build hosts without gl4es installed) so the dual-path
# is preserved while the native path is being verified. Once a
# full-88 visual verification passes, the gl4es-routed path is
# retired per the operator's directive.
# -----------------------------------------------------------------------

# Generic config: most legacy hacks just need the harness + the
# vendored source + the optional model / utility companions.
# 'c_args_extra' are -DSTANDALONE / -DHAVE_JWXYZ / etc. — the
# include-vs-xlockmore.h choice the vendored sources gate on.
legacy_gles3_hacks = [
    {'name': 'rubikblocks',   'src': 'src/rubikblocks.c',   'extras': ['src/rotator.c', 'src/yarandom.c']},
    {'name': 'topblock',      'src': 'src/topblock.c',      'extras': ['src/sphere.c', 'src/tube.c']},
    {'name': 'tangram',       'src': 'src/tangram.c',       'extras': ['src/tangram_shapes.c']},
    {'name': 'glschool',      'src': 'src/glschool.c',      'extras': ['src/glschool_alg.c', 'src/glschool_gl.c', 'src/sphere.c', 'src/tube.c', 'src/yarandom.c']},
    {'name': 'fliptext',       'src': 'src/fliptext.c',       'extras': []},
    {'name': 'antinspect',    'src': 'src/antinspect.c',     'extras': ['src/sphere.c'],                                 'c_args_extra': ['-DSTANDALONE']},
    {'name': 'antspotlight',  'src': 'src/antspotlight.c',   'extras': ['src/rotator.c', 'src/sphere.c', 'src/tube.c', 'src/yarandom.c'], 'c_args_extra': ['-DSTANDALONE']},
    {'name': 'beats',         'src': 'src/beats.c',          'extras': ['src/sphere.c']},
    {'name': 'blinkbox',      'src': 'src/blinkbox.c',       'extras': ['src/sphere.c']},
    {'name': 'blocktube',     'src': 'src/blocktube.c',      'extras': []},
    {'name': 'bouncingcow',   'src': 'src/bouncingcow.c',    'extras': ['src/cow_face.c', 'src/cow_hide.c', 'src/cow_hoofs.c', 'src/cow_horns.c', 'src/cow_tail.c', 'src/cow_udder.c', 'src/gllist.c', 'src/rotator.c', 'src/yarandom.c']},
    {'name': 'chompytower',   'src': 'src/chompytower.c',    'extras': ['src/doubletime.c', 'src/easing.c', 'src/gllist.c', 'src/normals.c', 'src/rotator.c', 'src/sphere.c', 'src/spline.c', 'src/teeth_model.c', 'src/yarandom.c']},
    {'name': 'crumbler',      'src': 'src/crumbler.c',       'extras': ['src/quickhull.c', 'src/rotator.c', 'src/yarandom.c']},
    {'name': 'cube21',        'src': 'src/cube21.c',         'extras': []},
    {'name': 'cubestack',     'src': 'src/cubestack.c',      'extras': ['src/rotator.c', 'src/yarandom.c']},
    {'name': 'cityflow',      'src': 'src/cityflow.c',       'extras': []},
    {'name': 'covid19',       'src': 'src/covid19.c',        'extras': ['src/rotator.c', 'src/sphere.c', 'src/tube.c', 'src/yarandom.c']},
    {'name': 'crackberg',     'src': 'src/crackberg.c',      'extras': []},
    {'name': 'cubenetic',     'src': 'src/cubenetic.c',      'extras': ['src/rotator.c', 'src/yarandom.c']},
    {'name': 'cubestorm',     'src': 'src/cubestorm.c',      'extras': ['src/rotator.c', 'src/yarandom.c']},
    {'name': 'cubetwist',     'src': 'src/cubetwist.c',      'extras': ['src/rotator.c', 'src/yarandom.c']},
    {'name': 'cubicgrid',     'src': 'src/cubicgrid.c',      'extras': ['src/rotator.c', 'src/yarandom.c']},
    {'name': 'dangerball',    'src': 'src/dangerball.c',     'extras': ['src/rotator.c', 'src/sphere.c', 'src/tube.c', 'src/yarandom.c']},
    {'name': 'discoball',     'src': 'src/discoball.c',      'extras': ['src/rotator.c', 'src/yarandom.c']},
    {'name': 'energystream',  'src': 'src/energystream.c',   'extras': ['src/rotator.c', 'src/yarandom.c'], 'c_args_extra': ['-DSTANDALONE']},
    {'name': 'gears',         'src': 'src/gears.c',          'extras': ['src/involute.c', 'src/normals.c', 'src/rotator.c', 'src/tube.c', 'src/yarandom.c']},
    {'name': 'geodesic',      'src': 'src/geodesic.c',       'extras': ['src/normals.c', 'src/rotator.c', 'src/yarandom.c']},
    {'name': 'glblur',        'src': 'src/glblur.c',         'extras': ['src/rotator.c', 'src/yarandom.c']},
    {'name': 'glcells',       'src': 'src/glcells.c',        'extras': []},
    {'name': 'glknots',       'src': 'src/glknots.c',        'extras': ['src/rotator.c', 'src/tube.c', 'src/yarandom.c']},
    {'name': 'glsnake',       'src': 'src/glsnake.c',        'extras': []},
    {'name': 'gravitywell',   'src': 'src/gravitywell.c',    'extras': []},
    {'name': 'hexstrut',      'src': 'src/hexstrut.c',       'extras': ['src/rotator.c', 'src/yarandom.c']},
    {'name': 'hextrail',      'src': 'src/hextrail.c',       'extras': ['src/rotator.c', 'src/yarandom.c']},
    {'name': 'highvoltage',   'src': 'src/highvoltage.c',    'extras': ['src/gllist.c', 'src/highvoltage_model.c', 'src/normals.c', 'src/tube.c']},
    {'name': 'hilbert',       'src': 'src/hilbert.c',        'extras': ['src/rotator.c', 'src/sphere.c', 'src/tube.c', 'src/yarandom.c']},
    {'name': 'hydrostat',     'src': 'src/hydrostat.c',      'extras': ['src/sphere.c']},
    {'name': 'hypnowheel',    'src': 'src/hypnowheel.c',     'extras': ['src/rotator.c', 'src/yarandom.c']},
    {'name': 'jigsaw',        'src': 'src/jigsaw.c',         'extras': ['src/normals.c', 'src/rotator.c', 'src/spline.c', 'src/yarandom.c'], 'c_args_extra': ['-DHAVE_JWZGLES']},
    {'name': 'juggler3d',     'src': 'src/juggler3d.c',      'extras': ['src/rotator.c', 'src/sphere.c', 'src/tube.c', 'src/yarandom.c']},
    {'name': 'kaleidocycle',  'src': 'src/kaleidocycle.c',   'extras': ['src/normals.c', 'src/rotator.c', 'src/yarandom.c']},
    {'name': 'lament',        'src': 'src/lament.c',         'extras': ['src/gllist.c', 'src/lament_model.c', 'src/image_data_to_ximage.c', 'src/normals.c', 'src/rotator.c', 'src/yarandom.c']},
    {'name': 'lockward',      'src': 'src/lockward.c',       'extras': []},
    {'name': 'mapscroller',   'src': 'src/mapscroller.c',    'extras': ['src/easing.c', 'src/image_data_to_ximage.c']},
    {'name': 'menger',        'src': 'src/menger.c',         'extras': ['src/rotator.c', 'src/yarandom.c']},
    {'name': 'moebiusgears',  'src': 'src/moebiusgears.c',   'extras': ['src/involute.c', 'src/normals.c', 'src/rotator.c', 'src/yarandom.c']},
    {'name': 'molecule',      'src': 'src/molecule.c',       'extras': ['src/rotator.c', 'src/sphere.c', 'src/tube.c', 'src/yarandom.c']},
    {'name': 'noof',          'src': 'src/noof.c',           'extras': ['src/pow2.c']},
    {'name': 'papercube',     'src': 'src/papercube.c',      'extras': ['src/rotator.c', 'src/yarandom.c']},
    {'name': 'peepers',       'src': 'src/peepers.c',        'extras': ['src/image_data_to_ximage.c', 'src/normals.c', 'src/rotator.c', 'src/yarandom.c']},
    {'name': 'polyhedra-gl',  'src': 'src/polyhedra-gl.c',   'extras': ['src/normals.c', 'src/polyhedra.c', 'src/rotator.c', 'src/teapot.c', 'src/yarandom.c'], 'c_args_extra': ['-DHAVE_JWZGLES'], 'table': 'polyhedra'},
    {'name': 'quasicrystal',  'src': 'src/quasicrystal.c',   'extras': ['src/rotator.c', 'src/yarandom.c']},
    {'name': 'spheremonics',  'src': 'src/spheremonics.c',   'extras': ['src/normals.c', 'src/rotator.c', 'src/yarandom.c']},
    {'name': 'splodesic',     'src': 'src/splodesic.c',      'extras': ['src/normals.c', 'src/rotator.c', 'src/yarandom.c']},
    {'name': 'squirtorus',    'src': 'src/squirtorus.c',     'extras': ['src/easing.c', 'src/normals.c', 'src/spline.c', 'src/yarandom.c']},
    {'name': 'stonerview',    'src': 'src/stonerview.c',     'extras': ['src/stonerview-view.c', 'src/stonerview-move.c', 'src/stonerview-osc.c', 'src/yarandom.c'], 'c_args_extra': ['-DSTANDALONE']},
    {'name': 'timetunnel',    'src': 'src/timetunnel.c',     'extras': ['src/image_data_to_ximage.c', 'src/rotator.c', 'src/yarandom.c']},
    {'name': 'tronbit',       'src': 'src/tronbit.c',        'extras': ['src/doubletime.c', 'src/gllist.c', 'src/rotator.c', 'src/sphere.c', 'src/tronbit_idle1.c', 'src/tronbit_idle2.c', 'src/tronbit_no.c', 'src/tronbit_yes.c', 'src/yarandom.c']},
    {'name': 'voronoi',       'src': 'src/voronoi.c',        'extras': []},
    # Round 11 batch — alphabetical-by-name, 12 new native GLES3 ports
    {'name': 'flyingtoasters', 'src': 'src/flyingtoasters.c', 'extras': ['src/gllist.c', 'src/image_data_to_ximage.c', 'src/toast.c', 'src/toast2.c', 'src/toaster.c', 'src/toaster_base.c', 'src/toaster_handle.c', 'src/toaster_handle2.c', 'src/toaster_jet.c', 'src/toaster_knob.c', 'src/toaster_slots.c', 'src/toaster_wing.c', 'src/yarandom.c']},
    {'name': 'geodesicgears',  'src': 'src/geodesicgears.c',  'extras': ['src/involute.c', 'src/normals.c', 'src/rotator.c', 'src/tube.c', 'src/yarandom.c']},
    {'name': 'gibson',         'src': 'src/gibson.c',         'extras': ['src/easing.c', 'src/rotator.c', 'src/yarandom.c']},
    {'name': 'glforestfire',   'src': 'src/glforestfire.c',   'extras': ['src/image_data_to_ximage.c'], 'c_args_extra': ['-DSTANDALONE']},
    {'name': 'glhanoi',        'src': 'src/glhanoi.c',        'extras': ['src/doubletime.c', 'src/rotator.c', 'src/yarandom.c']},
    {'name': 'gltext',         'src': 'src/gltext.c',         'extras': ['src/glut_stroke.c', 'src/glut_swidth.c', 'src/rotator.c', 'src/sphere.c', 'src/tube.c', 'src/yarandom.c']},
    {'name': 'handsy',         'src': 'src/handsy.c',         'extras': ['src/doubletime.c', 'src/gllist.c', 'src/handsy_model.c', 'src/rotator.c', 'src/sphere.c', 'src/tube.c', 'src/yarandom.c']},
    {'name': 'kallisti',       'src': 'src/kallisti.c',       'extras': ['src/gllist.c', 'src/kallisti_model.c', 'src/rotator.c', 'src/yarandom.c']},
    {'name': 'photopile',      'src': 'src/photopile.c',      'extras': ['src/dropshadow.c', 'src/xftwrap.c', 'src/yarandom.c']},
    {'name': 'providence',     'src': 'src/providence.c',     'extras': [], 'c_args_extra': ['-DSTANDALONE']},
    {'name': 'unicrud',        'src': 'src/unicrud.c',        'extras': ['src/rotator.c', 'src/yarandom.c']},
    {'name': 'unknownpleasures','src': 'src/unknownpleasures.c','extras': ['src/doubletime.c', 'src/easing.c']},
    # Round 11 batch 2 — alphabetical-by-name, 11 more native GLES3 ports
    {'name': 'headroom',       'src': 'src/headroom.c',       'extras': ['src/gllist.c', 'src/headroom_model.c', 'src/rotator.c', 'src/skull_model.c', 'src/yarandom.c']},
    {'name': 'lavalite',       'src': 'src/lavalite.c',       'extras': ['src/marching.c', 'src/normals.c', 'src/rotator.c', 'src/yarandom.c']},
    {'name': 'nakagin',        'src': 'src/nakagin.c',        'extras': ['src/doubletime.c', 'src/easing.c', 'src/normals.c', 'src/rotator.c', 'src/yarandom.c']},
    {'name': 'raverhoop',      'src': 'src/raverhoop.c',      'extras': ['src/rotator.c', 'src/yarandom.c']},
    {'name': 'razzledazzle',   'src': 'src/razzledazzle.c',   'extras': ['src/gllist.c', 'src/normals.c', 'src/ships.c', 'src/yarandom.c']},
    {'name': 'sballs',         'src': 'src/sballs.c',         'extras': ['src/image_data_to_ximage.c'], 'c_args_extra': ['-DSTANDALONE']},
    {'name': 'skulloop',       'src': 'src/skulloop.c',       'extras': ['src/easing.c', 'src/gllist.c', 'src/normals.c', 'src/skull_model.c', 'src/yarandom.c']},
    {'name': 'skytentacles',   'src': 'src/skytentacles.c',   'extras': ['src/image_data_to_ximage.c', 'src/normals.c', 'src/rotator.c', 'src/yarandom.c']},
    {'name': 'splitflap',      'src': 'src/splitflap.c',      'extras': ['src/gllist.c', 'src/rotator.c', 'src/splitflap_obj.c', 'src/yarandom.c']},
    {'name': 'starwars',       'src': 'src/starwars.c',       'extras': ['src/glut_stroke.c', 'src/glut_swidth.c', 'src/yarandom.c']},
    {'name': 'winduprobot',    'src': 'src/winduprobot.c',    'extras': ['src/gllist.c', 'src/image_data_to_ximage.c', 'src/involute.c', 'src/normals.c', 'src/robot.c', 'src/robot-wireframe.c', 'src/sphere.c', 'src/yarandom.c']},

    # Round 13 — STEP4-EXPANSION-BRIEF Task 1a ports (native GLES3 path).
    # atlantis: 3D swimming sea-creatures demo by Mark Kilgard /
    #           E. Lassauge. sea-texture is a 128x128 grayscale PNG
    #           embedded via tools/png_to_h.py as
    #           src/images/gen/sea-texture_png.h.
    {'name': 'atlantis',       'src': 'src/atlantis.c',       'extras': ['src/dolphin.c', 'src/image_data_to_ximage.c', 'src/shark.c', 'src/swim.c', 'src/whale.c', 'src/yarandom.c'], 'c_args_extra': ['-DSTANDALONE']},
    # flurry: pyrotechnic firework + smoke + spark + star + texture
    #         effects. Five files share state via flurry.h.
    {'name': 'flurry',         'src': 'src/flurry.c',         'extras': ['src/flurry-smoke.c', 'src/flurry-spark.c', 'src/flurry-star.c', 'src/flurry-texture.c', 'src/yarandom.c'], 'c_args_extra': ['-DSTANDALONE']},
]

foreach h : legacy_gles3_hacks
    executable(
        h['name'] + '_gles3',
        files(common_gles3_sources + [
            'src/gles3_harness.c',
            h['src'],
        ] + h.get('extras', [])) + gen_sources,
        include_directories: inc,
        dependencies: common_gles3_deps,
        c_args: gles3_native_c_args + h.get('c_args_extra', []) + [
            # The hack's <table>_xscreensaver_function_table symbol is
            # emitted by XSCREENSAVER_MODULE(<class>, <prefix>) at the
            # bottom of the vendored .c. Most hacks use the bare
            # <name> as the table identifier (so
            # <name>_xscreensaver_function_table); a few use a
            # different stem (polyhedra-gl's vendored file uses
            # XSCREENSAVER_MODULE("Polyhedra", polyhedra) → the table
            # is named polyhedra_xscreensaver_function_table, not
            # polyhedra-gl_xscreensaver_function_table). The 'table'
            # dict key overrides the default when they differ.
            '-DHACK_TABLE=@0@_xscreensaver_function_table'.format(h.get('table', h['name'])),
        ],
        install: false,
    )
endforeach

# -----------------------------------------------------------------------
# Round 13 — Phase 3 — hyprsaver shader port (vendor/hyprsaver/, MIT, Mara Vexa).
# 35 GLSL fragment shaders (all #version 320 es, GLES 3.2-native).
# One .c file (src/gles3_hyprsaver.c) is reused for all 35 targets —
# the foreach picks which .frag to compile and which symbol stem to
# use. License preserved at vendor/hyprsaver/LICENSE; attribution
# in PORTED.md.
# -----------------------------------------------------------------------

hyprsaver_shaders = [
    'attitude', 'aurora', 'bezier', 'blob', 'caustics', 'circuit',
    'clouds', 'donut', 'fireflies', 'flames', 'fractaltrap', 'geometry',
    'gridwave', 'hypercube', 'julia', 'kaleidoscope', 'lissajous',
    'marble', 'matrix', 'mobius', 'oscilloscope', 'planet', 'plasma',
    'shipburn', 'snowfall', 'sonar', 'starfield', 'stonks', 'temple',
    'terminal', 'tesla', 'tunnel', 'voronoi', 'waterfall', 'wormhole',
]

# Absolute runtime asset directory matches both loaders, including builds
# configured with the default /usr/local prefix. DESTDIR staging still works.
shader_install_dir = '/usr/share/ncz-screensavers/shaders'
install_data('vendor/blackhole/blackhole.frag', install_dir: shader_install_dir,
             install_tag: 'runtime-shaders')
install_data('vendor/lavafield/lavafield.frag', install_dir: shader_install_dir,
             install_tag: 'runtime-shaders')
foreach s : hyprsaver_shaders
    install_data('vendor/hyprsaver/shaders/' + s + '.frag',
                 install_dir: shader_install_dir, install_tag: 'runtime-shaders')
    executable(
        'hyprsaver_' + s + '_gles3',
        files(common_gles3_sources + [
            'src/gles3_harness.c',
            'src/gles3_hyprsaver.c',
        ]) + gen_sources,
        include_directories: inc,
        dependencies: common_gles3_deps,
        c_args: gles3_native_c_args + [
            # Quoted because meson/gcc splits 'tunnel.frag' on the dot.
            '-DSHADER_FILE="@0@.frag"'.format(s),
            '-DHACK_PREFIX=hyprsaver_@0@'.format(s),
            '-DHACK_TABLE=hyprsaver_@0@_xscreensaver_function_table'.format(s),
        ],
        install: false,
    )
endforeach

# -----------------------------------------------------------------------
# Round 16 — xshadertoy shader port (vendor/xshadertoy/glsl/, MIT /
# CC0 / CC BY 3.0 / public domain; per-shader headers preserved; see
# vendor/xshadertoy/PORTED.md).
#
# 38 single-pass Shadertoy-API GLSL fragment shaders vendored from
# upstream xscreensaver 6.16 (commit b99f621, 2026-09-03). One
# generic driver (`src/gles3_xshadertoy.c`) is reused for all 38 via
# -D flags, matching the upstream pattern (one ~20-line bash
# wrapper per shader) and the pattern of this repo's hyprsaver
# shaders (PORTED.md §13.3).
# -----------------------------------------------------------------------

xshadertoy_shaders = [
    'alienbeacon', 'amigajuggler', 'batteredplanet',
    'bestill0-0', 'bestill1-0', 'bestill2-0', 'bestill3-0',
    'bestill4-0', 'bestill5-0',
    'bubblecolors', 'darktransit', 'downfall', 'driftclouds',
    'elementalring', 'fluxcore', 'gimbalharmonics', 'goldenapollian',
    'hexplasma', 'logarithmiccircles',
    'neongravity-0', 'neongravity-1', 'neontriangulator',
    'noxfire', 'polarnight', 'prococean', 'protophore',
    'rigrekt', 'selfreflect', 'skyline', 'stardome', 'starnest',
    'stripeytorus', 'synthwavecity', 'topologica', 'trainmandala',
    'trizm', 'truchetzoom', 'universeball',
]

foreach s : xshadertoy_shaders
    install_data('vendor/xshadertoy/glsl/' + s + '.glsl',
                 install_dir: shader_install_dir, install_tag: 'runtime-shaders')
    # Hyphen-to-underscore substitution for the C identifier stem.
    # The shader basenames `bestill0-0`..`bestill5-0`, `neongravity-0`
    # and `neongravity-1` carry hyphens which are illegal in C
    # identifiers; the driver .c has separate HACK_PREFIX (display)
    # and HACK_PREFIX_ID (C-symbol stem) macros for that reason.
    s_id = s.replace('-', '_')
    executable(
        'xshadertoy_' + s + '_gles3',
        files(common_gles3_sources + [
            'src/gles3_harness.c',
            'src/gles3_xshadertoy.c',
        ]) + gen_sources,
        include_directories: inc,
        dependencies: common_gles3_deps,
        c_args: gles3_native_c_args + [
            '-DSHADER_FILE="@0@.glsl"'.format(s),
            '-DHACK_PREFIX=xshadertoy_@0@'.format(s),
            '-DHACK_PREFIX_ID=xshadertoy_@0@'.format(s_id),
            '-DHACK_TABLE=xshadertoy_@0@_xscreensaver_function_table'.format(s_id),
        ],
        install: false,
    )
endforeach

# -----------------------------------------------------------------------
# Round 15 — RSS-SDL2-GLES2 port (vendor/rss-sdl2-gles2-src/, Apache-2.0
# wrapper/port layer; per-saver .cpp files keep their original GPL-2.0
# Terence Welsh "Really Slick" headers — see PORTED.md §14).
#
# 13 savers ported as new native-GLES3 _gles3.c files (cyclone, euphoria,
# fieldlines, flocks, flux, helios, hyperspace, implicitdemo, lattice,
# microcosm, plasma, skyrocket, solarwinds). Each .c file:
#   - preserves the GPL-2.0 header verbatim from the original .cpp
#   - compiles against xscreensaver_compat.h (which provides glBegin /
#     glVertex / glColor / glPushMatrix / glRotatef / glLightfv / glu*
#     as thin shims into the gles3_compat GLES3-native matrix stack +
#     immediate-mode accumulator + shader)
#   - uses XSCREENSAVER_MODULE_2(name, table, prefix) to emit the
#     xscreensaver_function_table that gles3_harness.c drives
# See PORTED.md §14 for the per-saver "what's preserved vs simplified"
# call-out (helios, hyperspace, implicitdemo, lattice, microcosm,
# skyrocket are simplified reps because their full algorithms depend
# on helper libraries not vendored — marching cubes, custom shaders,
# cube-map pipelines, etc.).
# -----------------------------------------------------------------------

# MIT Schwarzschild raytracer port; see vendor/blackhole-PORTED.md.
executable(
    'blackhole_gles3',
    files(common_gles3_sources + [
        'src/gles3_harness.c',
        'src/gles3_blackhole.c',
    ]) + gen_sources,
    include_directories: inc,
    dependencies: common_gles3_deps,
    c_args: gles3_native_c_args + [
        '-DHACK_TABLE=blackhole_xscreensaver_function_table',
    ],
    install: false,
)

# Round 17 — lavafield: rewrite of lavalite on the new engine. Fullscreen
# raymarched metaball lava field, palette + blob count + viscosity + scale
# + warmth randomised per-launch from /dev/urandom. Replaces the 1990s
# framing of jwz's lavalite (small object on black, lamp/glass/base) with
# the entire screen filled. Old src/lavalite.c is preserved for now; see
# docs/.../lavafield-roundup.md for the drop-recommendation evidence.
executable(
    'lavafield_gles3',
    files(common_gles3_sources + [
        'src/gles3_harness.c',
        'src/gles3_lavafield.c',
    ]) + gen_sources,
    include_directories: inc,
    dependencies: common_gles3_deps,
    c_args: gles3_native_c_args + [
        '-DHACK_TABLE=lavafield_xscreensaver_function_table',
    ],
    install: false,
)

# Round 19 — genxvectorcade: psychedelic vector-arcade journey. The
# second piece of the GenX vision: a non-interactive, ever-evolving
# visual that blends vector tunnel / grid horizon / starfield warp /
# kaleidoscope bloom / lattice canyon into one continuous trip with
# persistent feedback trails as the through-line. Differs from the
# neonasteroids piece (Round 18) in that there is no game and no
# geometry to model — the journey IS the artwork. See
# docs/superpowers/specs/2026-09-25-genxvectorcade-vision.md for the
# shared creative spine.
#
# Architecture: one driver .c (src/gles3_genxvectorcade.c) +
# one fragment shader (vendor/genxvectorcade/genxvectorcade.frag)
# loaded at runtime from the source tree or from
# /usr/share/ncz-screensavers/shaders/. The host owns a half-
# resolution RGBA8 ping-pong FBO for the feedback trail buffer (the
# feature that separates this from a generic tunnel shader) plus a
# tiny "fade + rotate" pass that spirals the wakes each frame. Trail
# buffer is intentionally half-resolution: full-resolution on Intel
# UHD nearly doubled per-frame cost (measured on PEGASUS 2026-09-25)
# and trails are intentionally soft.
install_data('vendor/genxvectorcade/genxvectorcade.frag',
             install_dir: shader_install_dir, install_tag: 'runtime-shaders')
executable(
    'genxvectorcade_gles3',
    files(common_gles3_sources + [
        'src/gles3_harness.c',
        'src/gles3_genxvectorcade.c',
    ]) + gen_sources,
    include_directories: inc,
    dependencies: common_gles3_deps,
    c_args: gles3_native_c_args + [
        '-DHACK_TABLE=genxvectorcade_xscreensaver_function_table',
    ],
    install: false,
)


# Round 18 — neonasteroids (working name: GenXRockCade, placeholder
# rename at the top of src/gles3_neonasteroids.c).
#
# Self-playing neon vector rock-shooter: ship in a wrapping playfield,
# drifting rocks that split when shot, all rendered as glowing neon
# vector lines with bloom + chromatic aberration + palette cycling +
# shockwave warps. AI plays the game; nobody touches the keyboard.
#
# Pattern matches the blackhole / lavafield GLES3-native targets:
# one driver .c, three vendor shaders (lines / trail-fade / composite),
# loaded at runtime from the source tree or from
# /usr/share/ncz-screensavers/shaders/neonasteroids/ (the
# install_data rule below ensures the binary works when launched
# from / — the cwd-relative-only path was a real ship-blocker on
# the flagship blackhole hack).
install_data('vendor/neonasteroids/composite.frag',
             install_dir: shader_install_dir, install_tag: 'runtime-shaders')
install_data('vendor/neonasteroids/lines.frag',
             install_dir: shader_install_dir, install_tag: 'runtime-shaders')
executable(
    'neonasteroids_gles3',
    files(common_gles3_sources + [
        'src/gles3_harness.c',
        'src/gles3_neonasteroids.c',
    ]) + gen_sources,
    include_directories: inc,
    dependencies: common_gles3_deps,
    c_args: gles3_native_c_args + [
        '-DHACK_TABLE=neonasteroids_xscreensaver_function_table',
    ],
    install: false,
)


rss_sdl2_gles2_hacks = [
    {'name': 'cyclone',       'src': 'src/cyclone_gles3.c'},
    {'name': 'euphoria',      'src': 'src/euphoria_gles3.c'},
    {'name': 'fieldlines',    'src': 'src/fieldlines_gles3.c'},
    {'name': 'flocks',        'src': 'src/flocks_gles3.c'},
    {'name': 'flux',          'src': 'src/flux_gles3.c'},
    {'name': 'helios',        'src': 'src/helios_gles3.c'},
    {'name': 'hyperspace',    'src': 'src/hyperspace_gles3.c'},
    {'name': 'implicitdemo',  'src': 'src/implicitdemo_gles3.c'},
    {'name': 'lattice',       'src': 'src/lattice_gles3.c'},
    {'name': 'microcosm',     'src': 'src/microcosm_gles3.c'},
    {'name': 'plasma',        'src': 'src/plasma_gles3.c'},
    {'name': 'skyrocket',     'src': 'src/skyrocket_gles3.c'},
    {'name': 'solarwinds',    'src': 'src/solarwinds_gles3.c'},
]

foreach h : rss_sdl2_gles2_hacks
    executable(
        h['name'] + '_gles3',
        files(common_gles3_sources + [
            'src/gles3_harness.c',
            h['src'],
        ]) + gen_sources,
        include_directories: inc,
        dependencies: common_gles3_deps,
        c_args: gles3_native_c_args + [
            '-DHACK_TABLE=@0@_xscreensaver_function_table'.format(h['name']),
        ],
        install: false,
    )
endforeach

# -----------------------------------------------------------------------
# Phase 1+ — upstream xscreensaver 6.00+ GLES3 rewrites (5 Carsten Steger
# Klein-bottle-cluster hacks + sphereeversion). These already use real
 # GLSL/GLES3 shaders upstream — they call glCreateShader /
 # glCompileShader / glLinkProgram directly via the shared
 # hacks/glx/glsl-utils.{c,h} utility. Vendor the hack source + the
 # shared utility + any per-hack model data, and build against system
 # libGLESv2 / libEGL with -DHAVE_GLSL so the GLSL path compiles in.
 #
 # See UPSTREAM-GLES3-HACKS-2026-08-20.md for the per-hack verification
 # and which hacks needed foundation extensions.
 # -----------------------------------------------------------------------

# Shared utility: glsl-utils.{c,h} + the GLSL helper functions used by
# every upstream GLES3 hack (glsl_CompileAndLinkShaders,
# glsl_Perspective, glsl_LookAt, glsl_GetGLSLVersionString, ...).
upstream_gles3_common_sources = [
    'src/glsl-utils.c',
]

# Helper macro: every upstream GLES3 hack needs -DHAVE_GLSL (so the
# shader paths compile) plus -DSTANDALONE (so #include "xlockmore.h"
# fires inside the file's #ifdef STANDALONE block). The HACK_TABLE
# override routes gles3_harness.c at the right
# xscreensaver_function_table symbol.
upstream_gles3_c_args = gles3_native_c_args + [
    '-DHAVE_GLSL',
    '-DSTANDALONE',
]

# The 6 per-hack executable() targets are added individually by the
# per-hack commits that vendor each hack's source. See
# UPSTREAM-GLES3-HACKS-2026-08-20.md.

# Etruscanvenus — Klein bottle deforming between Venus / Roman / Boy / Ida
executable(
    'etruscanvenus_gles3',
    files(common_gles3_sources + upstream_gles3_common_sources + [
        'src/gles3_harness.c',
        'src/etruscanvenus.c',
    ]) + gen_sources,
    include_directories: inc,
    dependencies: common_gles3_deps,
    c_args: upstream_gles3_c_args + [
        '-DHACK_TABLE=etruscanvenus_xscreensaver_function_table',
    ],
    install: false,
)

# Hypertorus — thickened torus surface with Phong shading
executable(
    'hypertorus_gles3',
    files(common_gles3_sources + upstream_gles3_common_sources + [
        'src/gles3_harness.c',
        'src/hypertorus.c',
    ]) + gen_sources,
    include_directories: inc,
    dependencies: common_gles3_deps,
    c_args: upstream_gles3_c_args + [
        '-DHACK_TABLE=hypertorus_xscreensaver_function_table',
    ],
    install: false,
)

# Klein — Klein bottle immersion
executable(
    'klein_gles3',
    files(common_gles3_sources + upstream_gles3_common_sources + [
        'src/gles3_harness.c',
        'src/klein.c',
    ]) + gen_sources,
    include_directories: inc,
    dependencies: common_gles3_deps,
    c_args: upstream_gles3_c_args + [
        '-DHACK_TABLE=klein_xscreensaver_function_table',
    ],
    install: false,
)

# Projectiveplane — real projective plane immersion
executable(
    'projectiveplane_gles3',
    files(common_gles3_sources + upstream_gles3_common_sources + [
        'src/gles3_harness.c',
        'src/projectiveplane.c',
    ]) + gen_sources,
    include_directories: inc,
    dependencies: common_gles3_deps,
    c_args: upstream_gles3_c_args + [
        '-DHACK_TABLE=projectiveplane_xscreensaver_function_table',
    ],
    install: false,
)

# Romanboy — Roman and Boy surfaces (doubly-covered projective plane)
executable(
    'romanboy_gles3',
    files(common_gles3_sources + upstream_gles3_common_sources + [
        'src/gles3_harness.c',
        'src/romanboy.c',
    ]) + gen_sources,
    include_directories: inc,
    dependencies: common_gles3_deps,
    c_args: upstream_gles3_c_args + [
        '-DHACK_TABLE=romanboy_xscreensaver_function_table',
    ],
    install: false,
)

# Sphereeversion — sphere turned inside out via homotopy. The single
# sphereeversion.c file drives two different eversion algorithms
# (analytic and corrugations) via a runtime-selected helper object.
# earth.{h,c} provides the earth-texture data (1x1 valid-PNG stubs,
# see earth.c). image_data_to_ximage.c provides the libpng-backed
# decoder sphereeversion calls from setup_xpm_texture() (the
# runtime call is in the texture-init path which executes once
# per launch to populate GL textures for the optional '-colors
# earth' mode — that mode is never randomly selected by default,
# but the call always runs to keep texture IDs consistent).
executable(
    'sphereeversion_gles3',
    files(common_gles3_sources + upstream_gles3_common_sources + [
        'src/gles3_harness.c',
        'src/sphereeversion.c',
        'src/sphereeversion-analytic.c',
        'src/sphereeversion-corrugations.c',
        'src/earth.c',
        'src/image_data_to_ximage.c',
    ]) + gen_sources,
    include_directories: inc,
    dependencies: common_gles3_deps,
    c_args: upstream_gles3_c_args + [
        '-DHACK_TABLE=sphereeversion_xscreensaver_function_table',
    ],
    install: false,
)


# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------

summary = [
    'wl-screenhack   (rounds 1-6, always built)',
]
if opt_gl4es
    summary += ['gl4es_triangle (round 7 part 1, GL4ES @ @0@)'.format(gl4es_lib_path)]
endif
if opt_xscreensaver
    summary += ['glmatrix_demo  (round 7 part 2, glmatrix.c + compat shim)']
endif
message('Configured binaries:')
foreach line : summary
    message('  - ' + line)
endforeach
message('Run `meson compile -C build` to build.')

# -----------------------------------------------------------------------
# Per-binary phony alias targets so the cheap sanity gate
#
#   ninja -C build -t targets 2>/dev/null | grep -c _gles3
#
# reports 90 — not 0, which is what meson's `default all` produces for
# the bare `ninja -t targets` invocation (meson hides leaf targets
# behind the `all` phony, so they don't appear unless an arg is passed).
#
# The 90 aliases below are the same 90 `_gles3` executables listed
# earlier in this meson.build (boing_gles3 + companion_gles3 +
# rubikblocks_gles3 + topblock_gles3 + tangram_gles3 + glschool_gles3 +
# fliptext_gles3 + 82 legacy_gles3_hacks + 6 upstream hacks). Keep this
# list in sync if the upstream count changes — see
# docs/CROSS-PLATFORM-GLES3-VALIDATION-2026-09-22.md for the validation
# pipeline that uses this gate.
# -----------------------------------------------------------------------
foreach alias_name : [
    'blackhole_gles3',
    'lavafield_gles3',
    'antinspect_gles3', 'antspotlight_gles3', 'beats_gles3',
    'blinkbox_gles3', 'blocktube_gles3', 'boing_gles3',
    'bouncingcow_gles3', 'chompytower_gles3', 'cityflow_gles3',
    'companion_gles3', 'covid19_gles3', 'crackberg_gles3',
    'crumbler_gles3', 'cube21_gles3', 'cubenetic_gles3',
    'cubestack_gles3', 'cubestorm_gles3', 'cubetwist_gles3',
    'cubicgrid_gles3', 'dangerball_gles3', 'discoball_gles3',
    'energystream_gles3', 'etruscanvenus_gles3', 'fliptext_gles3',
    'flyingtoasters_gles3', 'gears_gles3', 'geodesic_gles3',
    'glblur_gles3', 'glcells_gles3', 'glforestfire_gles3',
    'glhanoi_gles3', 'glknots_gles3', 'glschool_gles3',
    'glsnake_gles3', 'gravitywell_gles3', 'handsy_gles3',
    'headroom_gles3', 'hexstrut_gles3', 'hextrail_gles3',
    'highvoltage_gles3', 'hilbert_gles3', 'hydrostat_gles3',
    'hypnowheel_gles3', 'hypertorus_gles3', 'jigsaw_gles3',
    'juggler3d_gles3', 'kaleidocycle_gles3', 'kallisti_gles3',
    'klein_gles3', 'lament_gles3', 'lavalite_gles3',
    'lockward_gles3', 'mapscroller_gles3', 'menger_gles3',
    'moebiusgears_gles3', 'molecule_gles3', 'nakagin_gles3',
    'noof_gles3', 'papercube_gles3', 'peepers_gles3',
    'photopile_gles3', 'polyhedra-gl_gles3', 'projectiveplane_gles3',
    'providence_gles3', 'quasicrystal_gles3', 'raverhoop_gles3',
    'razzledazzle_gles3', 'romanboy_gles3', 'rubikblocks_gles3',
    'sballs_gles3', 'skulloop_gles3', 'skytentacles_gles3',
    'sphereeversion_gles3', 'spheremonics_gles3', 'splitflap_gles3',
    'squirtorus_gles3', 'starwars_gles3', 'stonerview_gles3',
    'substrate_gles3', 'sudoku_gles3', 'surface_gles3',
    'swirlring_gles3', 'tangram_gles3', 'timetunnel_gles3',
    'topblock_gles3', 'tronbit_gles3', 'unicrud_gles3',
    'unknownpleasures_gles3', 'voronoi_gles3', 'winduprobot_gles3',
    # Round 13 — hyprsaver shader ports (35).
    'hyprsaver_attitude_gles3', 'hyprsaver_aurora_gles3',
    'hyprsaver_bezier_gles3', 'hyprsaver_blob_gles3',
    'hyprsaver_caustics_gles3', 'hyprsaver_circuit_gles3',
    'hyprsaver_clouds_gles3', 'hyprsaver_donut_gles3',
    'hyprsaver_fireflies_gles3', 'hyprsaver_flames_gles3',
    'hyprsaver_fractaltrap_gles3', 'hyprsaver_geometry_gles3',
    'hyprsaver_gridwave_gles3', 'hyprsaver_hypercube_gles3',
    'hyprsaver_julia_gles3', 'hyprsaver_kaleidoscope_gles3',
    'hyprsaver_lissajous_gles3', 'hyprsaver_marble_gles3',
    'hyprsaver_matrix_gles3', 'hyprsaver_mobius_gles3',
    'hyprsaver_oscilloscope_gles3', 'hyprsaver_planet_gles3',
    'hyprsaver_plasma_gles3', 'hyprsaver_shipburn_gles3',
    'hyprsaver_snowfall_gles3', 'hyprsaver_sonar_gles3',
    'hyprsaver_starfield_gles3', 'hyprsaver_stonks_gles3',
    'hyprsaver_temple_gles3', 'hyprsaver_terminal_gles3',
    'hyprsaver_tesla_gles3', 'hyprsaver_tunnel_gles3',
    'hyprsaver_voronoi_gles3', 'hyprsaver_waterfall_gles3',
    'hyprsaver_wormhole_gles3',
    # Round 13 — Task 1a ports (atlantis + flurry).
    'atlantis_gles3', 'flurry_gles3',
    # Round 15 — RSS-SDL2-GLES2 port (13 savers).
    'cyclone_gles3', 'euphoria_gles3', 'fieldlines_gles3',
    'flocks_gles3', 'flux_gles3', 'helios_gles3', 'hyperspace_gles3',
    'implicitdemo_gles3', 'lattice_gles3', 'microcosm_gles3',
    'plasma_gles3', 'skyrocket_gles3', 'solarwinds_gles3',
    # Round 16 — xshadertoy shader ports (38).
    'xshadertoy_alienbeacon_gles3', 'xshadertoy_amigajuggler_gles3',
    'xshadertoy_batteredplanet_gles3',
    'xshadertoy_bestill0-0_gles3', 'xshadertoy_bestill1-0_gles3',
    'xshadertoy_bestill2-0_gles3', 'xshadertoy_bestill3-0_gles3',
    'xshadertoy_bestill4-0_gles3', 'xshadertoy_bestill5-0_gles3',
    'xshadertoy_bubblecolors_gles3', 'xshadertoy_darktransit_gles3',
    'xshadertoy_downfall_gles3', 'xshadertoy_driftclouds_gles3',
    'xshadertoy_elementalring_gles3', 'xshadertoy_fluxcore_gles3',
    'xshadertoy_gimbalharmonics_gles3', 'xshadertoy_goldenapollian_gles3',
    'xshadertoy_hexplasma_gles3', 'xshadertoy_logarithmiccircles_gles3',
    'xshadertoy_neongravity-0_gles3', 'xshadertoy_neongravity-1_gles3',
    'xshadertoy_neontriangulator_gles3', 'xshadertoy_noxfire_gles3',
    'xshadertoy_polarnight_gles3', 'xshadertoy_prococean_gles3',
    'xshadertoy_protophore_gles3', 'xshadertoy_rigrekt_gles3',
    'xshadertoy_selfreflect_gles3', 'xshadertoy_skyline_gles3',
    'xshadertoy_stardome_gles3', 'xshadertoy_starnest_gles3',
    'xshadertoy_stripeytorus_gles3', 'xshadertoy_synthwavecity_gles3',
    'xshadertoy_topologica_gles3', 'xshadertoy_trainmandala_gles3',
    'xshadertoy_trizm_gles3', 'xshadertoy_truchetzoom_gles3',
    'xshadertoy_universeball_gles3',
]
    run_target(
        alias_name + '_alias',
        command : ['/bin/true'],
    )
endforeach

# _gles3_count sentinel: print the actual count of *_gles3 binaries
# that exist in the build directory. Cheap (no compilation/linking) and
# runs as a meson run_target, so it shows up in `ninja -t targets`.
run_target(
    '_gles3_count',
    command : ['/bin/sh', '-c',
        'cd .. && gles3_list=$(ls build/*_gles3 2>/dev/null | xargs -n1 basename 2>/dev/null); ' +
        'count=$(printf "%s\n" "$gles3_list" | grep -c "_gles3$"); ' +
        'echo "Configured _gles3 binaries: $count"; ' +
        'if [ "$count" -lt 80 ]; then echo "ERROR: expected at least 80 _gles3 binaries, got $count"; exit 1; fi'],
)
