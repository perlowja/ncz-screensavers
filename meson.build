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
endif

# libpng — required for xscreensaver-shim (PNG textures via image_data_to_ximage).
dep_png = dependency('libpng', required: opt_xscreensaver)

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
inc = include_directories('.')

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
        link_args: [
            '-L@0@'.format(gl4es_dir),
            '-lGL',
            '-Wl,-rpath,@0@'.format(gl4es_dir),
        ],
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
        link_args: [
            '-L@0@'.format(gl4es_dir),
            '-lGL',
            '-Wl,-rpath,@0@'.format(gl4es_dir),
        ],
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
        {'name': 'hypnowheel', 'src': 'src/hypnowheel.c', 'png': false, 'extra': ['src/rotator.c', 'src/yarandom.c']},
        {'name': 'cubicgrid', 'src': 'src/cubicgrid.c', 'png': false, 'extra': ['src/rotator.c', 'src/yarandom.c']},
        {'name': 'cubestack', 'src': 'src/cubestack.c', 'png': false, 'extra': ['src/rotator.c', 'src/yarandom.c']},
        {'name': 'glknots', 'src': 'src/glknots.c', 'png': false, 'extra': ['src/rotator.c', 'src/tube.c', 'src/yarandom.c']},
        {'name': 'cubestorm', 'src': 'src/cubestorm.c', 'png': false, 'extra': ['src/rotator.c', 'src/yarandom.c']},
        {'name': 'hexstrut', 'src': 'src/hexstrut.c', 'png': false, 'extra': ['src/rotator.c', 'src/yarandom.c']},
        {'name': 'energystream', 'src': 'src/energystream.c', 'png': false, 'extra': ['src/rotator.c', 'src/yarandom.c']},
        {'name': 'cubetwist', 'src': 'src/cubetwist.c', 'png': false, 'extra': ['src/rotator.c', 'src/yarandom.c']},
        {'name': 'splodesic', 'src': 'src/splodesic.c', 'png': false, 'extra': ['src/rotator.c', 'src/yarandom.c']},
        {'name': 'glblur', 'src': 'src/glblur.c', 'png': false, 'extra': ['src/rotator.c', 'src/yarandom.c']},
        {'name': 'rubikblocks', 'src': 'src/rubikblocks.c', 'png': false, 'extra': ['src/rotator.c', 'src/yarandom.c']},
        {'name': 'discoball', 'src': 'src/discoball.c', 'png': false, 'extra': ['src/rotator.c', 'src/yarandom.c']},
        {'name': 'raverhoop', 'src': 'src/raverhoop.c', 'png': false, 'extra': ['src/rotator.c', 'src/yarandom.c']},
        {'name': 'hextrail', 'src': 'src/hextrail.c', 'png': false, 'extra': ['src/rotator.c', 'src/yarandom.c']},
        {'name': 'crumbler', 'src': 'src/crumbler.c', 'png': false, 'extra': ['src/quickhull.c', 'src/rotator.c', 'src/yarandom.c']},
        {'name': 'lockward', 'src': 'src/lockward.c', 'png': false, 'extra': []},
        {'name': 'cube21', 'src': 'src/cube21.c', 'png': false, 'extra': []},
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
        executable(
            h['name'] + '_demo',
            files(hack_sources) + gen_sources,
            include_directories: inc,
            dependencies: [
                dep_wayland_client,
                dep_wayland_egl,
                dep_egl,
                dep_glesv2,
                dep_png,
            ],
            link_args: [
                '-L@0@'.format(gl4es_dir),
                '-lGL',
                '-Wl,-rpath,@0@'.format(gl4es_dir),
            ],
            c_args: vendored_c_args + [
                '-DHACK_TABLE=@0@_xscreensaver_function_table'.format(h['name']),
            ],
            install: false,
        )
    endforeach
endif

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