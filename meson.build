# meson.build - native Wayland/EGL GLES3 screensavers.
#
# Every binary is a GLES3 program driven by src/gles3_harness.c (Wayland
# layer-shell + EGL); nothing links OpenGL 1 emulation or gl4es.  The ship set
# below is what the ncz-screensavers package installs; the other targets are
# development helpers.

project(
    'ncz-screensavers',
    'c',
    default_options: [
        'c_std=c11',
        'warning_level=2',
        # Default -O0/-g; downstream packagers usually override.
    ],
    version: '0.7.3',
    license: 'GPL-2.0-or-later',
)

# ---------------------------------------------------------------------------
# Ship set (operator-ratified 2026-09-29) — the ONLY executables that are
# installed into the ncz-screensavers package. Everything else in this file
# builds for development/validation only (install: false).
# ---------------------------------------------------------------------------
ncz_libdir = get_option('prefix') / 'lib' / 'ncz-screensavers'
ncz_datadir = get_option('prefix') / 'share' / 'ncz-screensavers'
ncz_ship_bins = [
    'blackhole_gles3',
    'hyprsaver_attitude_gles3',
    'hyprsaver_aurora_gles3',
    'hyprsaver_bezier_gles3',
    'hyprsaver_blob_gles3',
    'hyprsaver_caustics_gles3',
    'hyprsaver_circuit_gles3',
    'hyprsaver_clouds_gles3',
    'hyprsaver_donut_gles3',
    'hyprsaver_fireflies_gles3',
    'hyprsaver_flames_gles3',
    'hyprsaver_fractaltrap_gles3',
    'hyprsaver_geometry_gles3',
    'hyprsaver_gridwave_gles3',
    'hyprsaver_hypercube_gles3',
    'hyprsaver_julia_gles3',
    'hyprsaver_kaleidoscope_gles3',
    'hyprsaver_lissajous_gles3',
    'hyprsaver_marble_gles3',
    'hyprsaver_matrix_gles3',
    'hyprsaver_mobius_gles3',
    'hyprsaver_oscilloscope_gles3',
    'hyprsaver_planet_gles3',
    'hyprsaver_plasma_gles3',
    'hyprsaver_shipburn_gles3',
    'hyprsaver_snowfall_gles3',
    'hyprsaver_sonar_gles3',
    'hyprsaver_starfield_gles3',
    'hyprsaver_stonks_gles3',
    'hyprsaver_temple_gles3',
    'hyprsaver_terminal_gles3',
    'hyprsaver_tesla_gles3',
    'hyprsaver_tunnel_gles3',
    'hyprsaver_voronoi_gles3',
    'hyprsaver_waterfall_gles3',
    'hyprsaver_wormhole_gles3',
    'xshadertoy_alienbeacon_gles3',
    'xshadertoy_batteredplanet_gles3',
    'xshadertoy_bestill0-0_gles3',
    'xshadertoy_bestill1-0_gles3',
    'xshadertoy_bestill2-0_gles3',
    'xshadertoy_bestill3-0_gles3',
    'xshadertoy_bestill4-0_gles3',
    'xshadertoy_bestill5-0_gles3',
    'xshadertoy_iridescence_gles3',
    'xshadertoy_darktransit_gles3',
    'xshadertoy_downfall_gles3',
    'xshadertoy_stratus_gles3',
    'xshadertoy_elementalring_gles3',
    'xshadertoy_fluxcore_gles3',
    'xshadertoy_gimbalharmonics_gles3',
    'xshadertoy_goldenapollian_gles3',
    'xshadertoy_hexplasma_gles3',
    'xshadertoy_logarithmiccircles_gles3',
    'xshadertoy_neongravity-1_gles3',
    'xshadertoy_neontriangulator_gles3',
    'xshadertoy_noxfire_gles3',
    'xshadertoy_polarnight_gles3',
    'xshadertoy_prococean_gles3',
    'xshadertoy_protophore_gles3',
    'xshadertoy_rigrekt_gles3',
    'xshadertoy_selfreflect_gles3',
    'xshadertoy_skyline_gles3',
    'xshadertoy_stardome_gles3',
    'xshadertoy_starnest_gles3',
    'xshadertoy_stripeytorus_gles3',
    'xshadertoy_neonhorizon_gles3',
    'xshadertoy_topologica_gles3',
    'xshadertoy_trainmandala_gles3',
    'xshadertoy_trizm_gles3',
    'xshadertoy_truchetzoom_gles3',
    'xshadertoy_universeball_gles3',
    'voronoi_gles3',
    'projectiveplane_gles3',
    'klein_gles3',
    'hypertorus_gles3',
    'cubestorm_gles3',
    'hexstrut_gles3',
    'crackberg_gles3',
    'cityflow_gles3',
    'geodesic_gles3',
    'gravitywell_gles3',
    'noof_gles3',
    'gibson_gles3',
]

# ---------------------------------------------------------------------------
# Build options — see `meson configure` for the full list.
# ---------------------------------------------------------------------------

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
add_project_arguments('-DNCZ_VERSION="@0@"'.format(meson.project_version()), language: 'c')

# libpng — used by the harness frame dump and the smoke testers.
dep_png = dependency('libpng', required: false)


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
    'viewporter':        'protocols/viewporter.xml',
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
inc = include_directories('.', 'src', 'include')

# Build-wide -Wall -Wextra. All our code (and the compat shim) must stay
# warning-clean under these. Vendored third-party code (xscreensaver
# hacks, GL4ES itself) is NOT held to this bar — they get c_args below
# that suppress relevant warnings where unavoidable.
common_c_args = ['-Wall', '-Wextra']

# -------------------------------------------------------------------
# GLES3 harness binaries
# -------------------------------------------------------------------

common_gles3_sources = [
    'src/ncz_hack_shim.c',
    'src/ncz_gl.c',
    'src/ncz_platform.c',
    'src/ncz_gpu_tier.c',
    'src/ncz_options.c',
    'src/ncz_render.c',
    'src/ncz_gpu_guard.c',
    'src/ncz_stats.c',
    'src/ncz_harness_cfg.c',
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

# Shader-engine ports of the xscreensaver classics (src/classics_shader/), built
# by the block at the end of this file.  The GL1 immediate-mode builds of these
# nine hacks (and the -Dlegacy-classics option) were retired; see
# docs/CLASSICS-SHADER-PORT.md.
classics_shader_ported = ['voronoi', 'gravitywell', 'hexstrut', 'cubestorm', 'crackberg', 'cityflow', 'noof', 'geodesic', 'gibson']
# extra vendored xscreensaver helper sources a port still needs (rotators, RNG)
classics_shader_extras = {
    'hexstrut': ['src/rotator.c', 'src/yarandom.c'],
    'cubestorm': ['src/rotator.c', 'src/yarandom.c'],
    'geodesic': ['src/rotator.c', 'src/yarandom.c'],
    'gibson': ['src/rotator.c', 'src/yarandom.c'],
}


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
shader_install_dir = ncz_datadir / 'shaders'
install_data('vendor/blackhole/blackhole.frag', install_dir: shader_install_dir,
             install_tag: 'runtime-shaders')
# Shaders of non-ship hacks are installed only with -Dinstall-extras=true
# (development installs); the ncz-screensavers package ships the ratified set.
install_extras = get_option('install-extras')
if install_extras
install_data('vendor/magmasimplex/magmasimplex.frag', install_dir: shader_install_dir,
             install_tag: 'runtime-shaders')
endif
foreach ms_shader : install_extras ? [
  'render', 'sim_zero', 'sim_advect_v', 'sim_advect_t', 'sim_advect_p',
  'sim_advect_h', 'sim_buoyancy', 'sim_project', 'sim_heat',
  'sim_phase', 'sim_init_seed',
  'sim3d_advect_v', 'sim3d_advect_t', 'sim3d_advect_p', 'sim3d_advect_h',
  'sim3d_buoyancy', 'sim3d_project', 'sim3d_heat', 'sim3d_phase',
  'sim3d_init_seed',
] : []
    install_data('vendor/magmasimplex/' + ms_shader + '.frag',
                 install_dir: shader_install_dir + '/magmasimplex',
                 install_tag: 'runtime-shaders')
endforeach
foreach s : hyprsaver_shaders
    if ncz_ship_bins.contains('hyprsaver_' + s + '_gles3')
        install_data('vendor/hyprsaver/shaders/' + s + '.frag',
                     install_dir: shader_install_dir, install_tag: 'runtime-shaders')
    endif
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
        install: ncz_ship_bins.contains('hyprsaver_' + s + '_gles3'), install_dir: ncz_libdir,
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
    'alienbeacon', 'batteredplanet',
    'bestill0-0', 'bestill1-0', 'bestill2-0', 'bestill3-0',
    'bestill4-0', 'bestill5-0',
    'caustictidepool', 'darktransit', 'downfall',
    'elementalring', 'fluxcore', 'gimbalharmonics', 'goldenapollian',
    'hexlattice', 'hexplasma', 'iridescence', 'logarithmiccircles',
    'neongravity-0', 'neongravity-1', 'neonhorizon', 'neontriangulator',
    'noxfire', 'polarnight', 'prococean', 'protophore',
    'rigrekt', 'selfreflect', 'skyline', 'stardome', 'starnest',
    'stratus', 'stripeytorus', 'topologica', 'trainmandala',
    'trizm', 'truchetzoom', 'universeball',
    'leviathan',
    'physarum',
    'attractor',
    'caustics',
    'aurora',
    'marble',
    'wellcurve',
]

# ridgeline.glsl stays in vendor/xshadertoy/glsl/ but is deliberately NOT
# installed: it is a calibration/stress shader, not catalogue content.
# Measured on an Intel UHD CML GT2 iGPU at 1920x1080 it managed 4 frames
# in 10s where cellmosaic and hexlattice both hit the 600-frame vsync cap,
# so it is useful for GPU-tier benchmarking and unusable as a screensaver.

foreach s : xshadertoy_shaders
    if ncz_ship_bins.contains('xshadertoy_' + s + '_gles3')
        install_data('vendor/xshadertoy/glsl/' + s + '.glsl',
                     install_dir: shader_install_dir, install_tag: 'runtime-shaders')
    endif
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
        install: ncz_ship_bins.contains('xshadertoy_' + s + '_gles3'), install_dir: ncz_libdir,
    )
endforeach

# -----------------------------------------------------------------------
# Round 20 — wave 2 quiet-materiality shaders. Six original .glsl files
# under vendor/wave2-materia/glsl/. The driver
# (src/gles3_xshadertoy.c) is the same one as above; its shader-search
# prefixes were extended with vendor/wave2-materia/glsl/ entries so the
# binary resolves its .glsl from build tree or repo root. Each shader is
# shipped in two places: the build tree (so dev iteration works) and
# /usr/share/ncz-screensavers/shaders/ on install.
#
# The six are independent GLES3-native fragment shaders, all written
# from scratch (no Shadertoy ports — its default CC BY-NC-SA licence
# is non-commercial and unshippable). Each follows the wave-2 brief:
# light RECEIVED rather than emitted. See
# docs/superpowers/specs/wave2-materia.md for the perceptual brief
# and the per-shader design intent.
# -----------------------------------------------------------------------

wave2_materia_shaders = [
    'caustics-on-stone',
    'satin-wind',
    'foliage-plaster',
    'frosted-glass',
    'thin-film',
    'aurora-restrained',
]

# -----------------------------------------------------------------------
# JurassicAquarium — procedural prehistoric sea at the waterline.
# Same generic driver as xshadertoy/wave2-materia; only the shader search
# prefix differs (src/gles3_xshadertoy.c was extended for
# vendor/jurassicaquarium/glsl/). Design doc:
# docs/superpowers/specs/2026-09-27-jurassicaquarium-design.md
#
# NOT installed: this is the skeleton build (current field + seaweed +
# waterline), measured on a dGPU only. It stays out of the catalogue until
# it has a real iGPU measurement. Building it is what makes that
# measurement possible at all.
# -----------------------------------------------------------------------

jurassicaquarium_shaders = [
    'jurassicaquarium_skeleton',
]

foreach s : jurassicaquarium_shaders
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
        install: ncz_ship_bins.contains('xshadertoy_' + s + '_gles3'), install_dir: ncz_libdir,
    )
endforeach

foreach s : wave2_materia_shaders
    if install_extras
        install_data('vendor/wave2-materia/glsl/' + s + '.glsl',
                     install_dir: shader_install_dir,
                     install_tag: 'runtime-shaders')
    endif
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
        install: ncz_ship_bins.contains('xshadertoy_' + s + '_gles3'), install_dir: ncz_libdir,
    )
endforeach

# -----------------------------------------------------------------------
# Round 19 — gl-transitions shader port (vendor/gl-transitions/,
# 123 MIT / 1 BSD-3-Clause / 1 BSD-2-Clause; per-file Author +
# License headers preserved; see vendor/gl-transitions-PORTED.md).
#
# 125 transition shaders vendored from gl-transitions/gl-transitions
# upstream commit 902218a (2026-09-25). One generic driver
# (`src/gles3_transitions.c`) is reused for all 125 via -D flags —
# the same shape as the xshadertoy driver above. Each transition
# compiles to one executable `gles3_transition_<name>_gles3`.
# Shaders install to /usr/share/ncz-screensavers/shaders/ alongside
# the other shader resources; the driver resolves them by searching
# build tree, then that absolute path. The "from /" launch path is
# exercised in evidence capture.
#
# Note: a transition is *not* a screensaver — see
# docs/superpowers/specs/2026-09-25-screensaver-family-doctrine.md
# for why. The driver binary is built and shipped as a transition
# infrastructure layer; it is exercised via NCZ_FRAME_DUMP-driven
# evidence captures and integrated with the chooser rotation /
# neonspacewar mode-switch path (separate commits).
# -----------------------------------------------------------------------

gles3_transitions_list = [
    'AdvancedMosaic', 'angular', 'BlockDissolve', 'BookFlip', 'Bounce',
    'BowTieHorizontal', 'BowTieVertical', 'BowTieWithParameter', 'Box',
    'burn', 'burn0', 'ButterflyWaveScrawler', 'cannabisleaf',
    'chessboard', 'circle', 'CircleCrop', 'circleopen', 'colorphase',
    'ColourDistance', 'coord-from-in', 'CrazyParametricFun',
    'crosshatch', 'crosswarp', 'CrossZoom', 'cube', 'DefocusBlur',
    'Directional', 'directional-easing', 'DirectionalScaled',
    'directionalwarp', 'directionalwipe', 'displacement', 'dissolve',
    'DoomScreenTransition', 'doorway', 'Dreamy', 'DreamyZoom',
    'Drop_Zone_Flicker', 'EdgeTransition', 'fade', 'fadecolor',
    'fadegrayscale', 'FilmBurn', 'flyeye', 'Fold', 'fragment',
    'GlitchDisplace', 'GlitchMemories', 'GridFlip', 'heart',
    'hexagonalize', 'HorizontalClose', 'HorizontalOpen', 'HSVfade',
    'InvertedPageCurl', 'kaleidoscope', 'LeftRight', 'LinearBlur',
    'luma', 'luminance_melt', 'morph', 'Mosaic', 'mosaic_transition',
    'multiply_blend', 'old_tv_lost_signal', 'Overexposure',
    'parametric_glitch', 'perlin', 'pinwheel', 'pixelize',
    'polar_function', 'PolkaDotsCurtain', 'powerKaleido', 'PuzzleRight',
    'Radial', 'randomNoisex', 'randomsquares', 'Rectangle',
    'RectangleCrop', 'Revolve_Left', 'ripple', 'Rolls',
    'rotate_scale_fade', 'RotateScaleVanish', 'rotateTransition',
    'scale-in', 'SimpleFlip', 'SimpleZoom', 'SimpleZoomOut', 'Slides',
    'splitSlideInHorizontal', 'splitSlideInOutHorizontal',
    'splitSlideInOutVertical', 'splitSlideInVertical',
    'splitSlideOutHorizontal', 'splitSlideOutVertical', 'squareswire',
    'squeeze', 'StarWipe', 'StaticFade', 'static_wipe', 'StereoViewer',
    'StripDatamoshGlitch', 'swap', 'Swirl', 'tangentMotionBlur',
    'TilesWave', 'TopBottom', 'TVStatic', 'undulatingBurnOut',
    'VerticalClose', 'VerticalOpen', 'WaterDrop', 'wind',
    'windowblinds', 'windowslice', 'wipeDown', 'wipeLeft', 'wipeRight',
    'wipeUp', 'x_axis_translation', 'ZoomInCircles', 'zoomInOut',
    'ZoomLeftWipe', 'ZoomRigthWipe',
]
assert(gles3_transitions_list.length() == 125,
       'gles3_transitions_list must contain exactly 125 transitions')

foreach t : gles3_transitions_list
    if install_extras
        install_data('vendor/gl-transitions/transitions/' + t + '.glsl',
                     install_dir: shader_install_dir,
                     install_tag: 'runtime-shaders')
    endif
    # Hyphens in upstream basenames (`coord-from-in`,
    # `directional-easing`, `scale-in`) break C identifiers — the
    # driver .c uses HACK_PREFIX for the display name and
    # HACK_PREFIX_ID for the C-symbol stem. Same substitution pattern
    # as the xshadertoy foreach above for `bestill0-0` etc.
    t_id = t.replace('-', '_')
    executable(
        'gles3_transition_' + t + '_gles3',
        files(common_gles3_sources + [
            'src/gles3_harness.c',
            'src/gles3_transitions.c',
        ]) + gen_sources,
        include_directories: inc,
        dependencies: common_gles3_deps,
        c_args: gles3_native_c_args + [
            '-DTRANSITION_FILE="@0@.glsl"'.format(t),
            '-DHACK_PREFIX=gles3_transition_@0@'.format(t),
            '-DHACK_PREFIX_ID=gles3_transition_@0@'.format(t_id),
            '-DHACK_TABLE=gles3_transition_@0@_xscreensaver_function_table'.format(t_id),
        ],
        install: ncz_ship_bins.contains('gles3_transition_' + t + '_gles3'), install_dir: ncz_libdir,
    )
endforeach

# Headless smoke tester for the 125 transitions. Reuses the same
# preamble/body-rewrite as the production driver but is a
# self-contained EGL+GLES3 program with no harness dependency.
# See docs/audit/gl-transitions-evidence/ROUNDUP-2026-09-26.md for
# what this is for and how to drive it.
executable(
    'gles3_transitions_smoke',
    files(['src/gles3_transitions_smoke.c']),
    include_directories: inc,
    dependencies: common_gles3_deps,
    c_args: gles3_native_c_args + [
        '-DSTANDALONE',
    ],
    install: ncz_ship_bins.contains('gles3_transitions_smoke'), install_dir: ncz_libdir,
)

# Headless smoke tester for xshadertoy-format fragment shaders under
# vendor/xshadertoy/glsl/. Same pattern as gles3_transitions_smoke
# (EGL_KHR_surfaceless_context, PBuffer destination, real PNG dumps
# via libpng). Validates compile/link/render for individual shaders
# without needing a Wayland compositor or the full gles3_harness.
# Usage: gles3_xshadertoy_smoke <shader.glsl> <out-dir> [t1 t2 ...]
executable(
    'gles3_xshadertoy_smoke',
    files(['src/gles3_xshadertoy_smoke.c']),
    include_directories: inc,
    dependencies: common_gles3_deps,
    c_args: gles3_native_c_args,
    install: ncz_ship_bins.contains('gles3_xshadertoy_smoke'), install_dir: ncz_libdir,
)

# MIT Schwarzschild raytracer port; see vendor/blackhole-PORTED.md.
blackhole_exe = executable(
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
    install: ncz_ship_bins.contains('blackhole_gles3'), install_dir: ncz_libdir,
)

# Round 18 — magmasimplex: rename + full rewrite of the lavafield
# prototype. Same line of work as lavafield (rising buoyant blobs in a
# coloured field, palette + blob count + viscosity + scale randomised
# per-launch) but:
#
#   - The "field" is now a real coloured transmissive liquid that fills
#     the frame, not blobs on black. Coverage target: >90% non-black.
#   - The blobs are translucent wax viewed THROUGH the liquid, so the
#     perceived hue is a product of the wax colour and the liquid hue
#     (Beer-Lambert-style transmission), not the wax composited over a
#     background. Yellow wax in blue liquid reads green on screen as
#     an emergent result of the optics, not by hardcoding green.
#   - Different blobs carry different hues simultaneously — the field
#     is multicoloured in a single frame, not monochrome.
#   - Lighting is a hot emitter low in frame with vertical luminance
#     falloff, not a uniform backlight. Wax pooled near the base is
#     the brightest thing on screen, frequently blown out.
#   - ~30 historical colourways (liquid / wax pairings) are randomised
#     per-launch from /dev/urandom. Internal names: ember, orchid,
#     ultraviolet, sunflower, opal. No manufacturer / brand / product
#     references in code, comments, docs, or commit messages.
#
# Replaces lavafield_gles3 (which measured 5% non-black and rendered
# monochrome on a mostly black field). Old src/lavalite.c and the
# lavalite_gles3 binary are dropped from the catalogue in this change —
# the legacy 1990s framing (small object on black) is now fully
# superseded. See docs/superpowers/rounds/2026-09-25-magmasimplex.md.
executable(
    'magmasimplex_gles3',
    files(common_gles3_sources + [
        'src/gles3_harness.c',
        'src/gles3_magmasimplex.c',
    ]) + gen_sources,
    include_directories: inc,
    dependencies: common_gles3_deps,
    c_args: gles3_native_c_args + [
        '-DHACK_TABLE=magmasimplex_xscreensaver_function_table',
    ],
    install: ncz_ship_bins.contains('magmasimplex_gles3'), install_dir: ncz_libdir,
)

# Round 19 — genxvectorcade: psychedelic vector-arcade journey. The
# second piece of the GenX vision: a non-interactive, ever-evolving
# visual that blends vector tunnel / grid horizon / starfield warp /
# kaleidoscope bloom / lattice canyon into one continuous trip with
# persistent feedback trails as the through-line. Differs from the
# neonspacewar piece (Round 18) in that there is no game and no
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
if install_extras
install_data('vendor/genxvectorcade/genxvectorcade.frag',
             install_dir: shader_install_dir, install_tag: 'runtime-shaders')
endif
executable(
    'genxvectorcade_gles3',
    files(common_gles3_sources + [
        'src/gles3_harness.c',
        'src/gles3_genxvectorcade.c',
        'src/ncz_platform.c',
    ]) + gen_sources,
    include_directories: inc,
    dependencies: common_gles3_deps,
    c_args: gles3_native_c_args + [
        '-DHACK_TABLE=genxvectorcade_xscreensaver_function_table',
    ],
    install: ncz_ship_bins.contains('genxvectorcade_gles3'), install_dir: ncz_libdir,
)


# Round 18 — neonspacewar (working name: GenXRockCade, placeholder
# rename at the top of src/gles3_neonspacewar.c).
#
# Self-playing neon vector rock-shooter: ship in a wrapping playfield,
# drifting rocks that split when shot, all rendered as glowing neon
# vector lines with bloom + chromatic aberration + palette cycling +
# shockwave warps. AI plays the game; nobody touches the keyboard.
#
# Pattern matches the blackhole / lavafield GLES3-native targets:
# one driver .c, three vendor shaders (lines / trail-fade / composite),
# loaded at runtime from the source tree or from
# /usr/share/ncz-screensavers/shaders/neonspacewar/ (the
# install_data rule below ensures the binary works when launched
# from / — the cwd-relative-only path was a real ship-blocker on
# the flagship blackhole hack).
if install_extras
install_data('vendor/neonspacewar/composite.frag',
             install_dir: shader_install_dir, install_tag: 'runtime-shaders')
install_data('vendor/neonspacewar/lines.frag',
             install_dir: shader_install_dir, install_tag: 'runtime-shaders')
endif
executable(
    'neonspacewar_gles3',
    files(common_gles3_sources + [
        'src/gles3_harness.c',
        'src/gles3_neonspacewar.c',
    ]) + gen_sources,
    include_directories: inc,
    dependencies: common_gles3_deps,
    c_args: gles3_native_c_args + [
        '-DHACK_TABLE=neonspacewar_xscreensaver_function_table',
    ],
    install: ncz_ship_bins.contains('neonspacewar_gles3'), install_dir: ncz_libdir,
)


# rss-sdl2 family REMOVED 2026-09-26 -- all 13 targets cut after operator
# visual judgement on real hardware: 1 of 5 reviewed was worth keeping, and
# that one (fireworks) is better rebuilt as a shader. See
# docs/superpowers/specs/2026-09-26-afterimage-design.md. The gles3_compat
# display-list/glColorMaterial fix (cf59926, 0e450e7) is UNRELATED and stays --
# it affected every GL1-path hack, not just these.


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
    '-DHAVE_JWZGLES',   # ES-only build: no desktop profile queries
    '-DHAVE_GLSL',
    '-DSTANDALONE',
]

# The 6 per-hack executable() targets are added individually by the
# per-hack commits that vendor each hack's source. See
# UPSTREAM-GLES3-HACKS-2026-08-20.md.

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
    install: ncz_ship_bins.contains('hypertorus_gles3'), install_dir: ncz_libdir,
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
    install: ncz_ship_bins.contains('klein_gles3'), install_dir: ncz_libdir,
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
    install: ncz_ship_bins.contains('projectiveplane_gles3'), install_dir: ncz_libdir,
)

# ---------------------------------------------------------------------------
# Chooser catalogue + GSettings schema (dev.ncz.screensaver).
# hacks.tsv is generated from ncz_ship_bins by tools/generate-catalog.py.
# ---------------------------------------------------------------------------
install_data('assets/screensaver-chooser/hacks.tsv',
             install_dir: get_option('prefix') / 'share' / 'ncz-screensaver-chooser')
# Shipped to both directories, like hacks.tsv: the launcher reads share/ncz-screensavers,
# the chooser plugin reads share/ncz-screensaver-chooser. Do not de-duplicate.
install_subdir('assets/screensaver-chooser/options/presets/blackhole',
               install_dir: get_option('prefix') / 'share' / 'ncz-screensavers' / 'presets')
install_data('assets/screensaver-chooser/render-hints.tsv',
             install_dir: get_option('prefix') / 'share' / 'ncz-screensavers')
install_data('assets/screensaver-chooser/render-hints.tsv',
             install_dir: get_option('prefix') / 'share' / 'ncz-screensaver-chooser')
install_data('assets/screensaver-chooser/presets.tsv',
             install_dir: get_option('prefix') / 'share' / 'ncz-screensavers')
install_data('assets/screensaver-chooser/presets.tsv',
             install_dir: get_option('prefix') / 'share' / 'ncz-screensaver-chooser')
install_data(['assets/screensaver-chooser/options/blackhole.tsv', 'assets/screensaver-chooser/options/_render.tsv'],
             install_dir: get_option('prefix') / 'share' / 'ncz-screensavers' / 'options')
install_data('assets/screensaver-chooser/sparse.tsv',
             install_dir: get_option('prefix') / 'share' / 'ncz-screensavers')
install_data('assets/screensaver-chooser/sparse.tsv',
             install_dir: get_option('prefix') / 'share' / 'ncz-screensaver-chooser')
# GPU tiers (see docs/LAUNCHER-DESIGN.md): the launcher and both settings UIs read them.
install_data('assets/screensaver-chooser/broken.tsv',
             install_dir: get_option('prefix') / 'share' / 'ncz-screensavers')
install_data('assets/screensaver-chooser/broken.tsv',
             install_dir: get_option('prefix') / 'share' / 'ncz-screensaver-chooser')
install_data('assets/screensaver-chooser/tiers.tsv',
             install_dir: get_option('prefix') / 'share' / 'ncz-screensavers')
install_data('assets/screensaver-chooser/tiers.tsv',
             install_dir: get_option('prefix') / 'share' / 'ncz-screensaver-chooser')
install_data('assets/screensaver-chooser/render-defaults.tsv',
             install_dir: get_option('prefix') / 'share' / 'ncz-screensavers')
install_data('config/dev.ncz.screensaver.gschema.xml',
             install_dir: get_option('prefix') / 'share' / 'glib-2.0' / 'schemas')

# Launcher, idle timer and settings glue live in launcher/ (own meson.build,
# maintained separately); pull them into the same install when present.
fs = import('fs')
if fs.is_dir('launcher') and fs.exists('launcher/meson.build')
    subdir('launcher')
endif

if get_option('singularity-plugin').enabled()
    subdir('plugin')
endif

if get_option('gtk4-reference-app')
    subdir('contrib/gtk4-reference-app')
endif

# ---------------------------------------------------------------------------
# Option parser unit test and blackhole schema sync test.
# ---------------------------------------------------------------------------
test_ncz_options = executable(
    'test_ncz_options',
    files(['tests/test_ncz_options.c', 'src/ncz_options.c']),
    include_directories: inc,
    c_args: ['-D_GNU_SOURCE'],
    dependencies: [dep_m],
    install: false,
)
test('ncz_options', test_ncz_options)
test_harness_cfg = executable(
    'test_harness_cfg',
    files(['tests/test_harness_cfg.c', 'src/ncz_render.c', 'src/ncz_gpu_guard.c', 'src/ncz_options.c']),
    include_directories: inc,
    c_args: ['-D_GNU_SOURCE'],
    dependencies: [dep_m],
    install: false,
)
test('harness_cfg', test_harness_cfg)
if not meson.is_cross_build()
    test('blackhole-schema-in-sync',
         find_program('sh'),
         args: ['-c', '"$0" --dump-schema | diff -u "$1" -',
                blackhole_exe, files('assets/screensaver-chooser/options/blackhole.tsv')])
    test('blackhole-preset-precedence',
         find_program('sh'),
         args: ['-c', 'cd "$1" && test "$($0 --preset=kerr --print-config | grep ^spin=)" = "spin=0.998" && test "$(NCZ_BLACKHOLE_SPIN=0.5 $0 --preset=kerr --print-config | grep ^spin=)" = "spin=0.5" && test "$(NCZ_BLACKHOLE_SPIN=0.5 $0 --preset=kerr --spin=0.7 --print-config | grep ^spin=)" = "spin=0.7" && test "$($0 --preset=nope --print-config 2>/dev/null | grep ^spin=)" = "spin=0"',
                blackhole_exe.full_path(), meson.project_source_root()])
    test('common-render-schema-in-sync',
         find_program('sh'),
         args: ['-c', '"$0" --dump-schema-common | diff -u "$1" -',
                blackhole_exe, files('assets/screensaver-chooser/options/_render.tsv')])
endif

# -----------------------------------------------------------------------
# Shader-engine ports of the xscreensaver classics.  One source file per
# hack in src/classics_shader/, same xscreensaver_function_table symbol as
# the GL1 original so the harness, catalog and packaging are unchanged.
# -----------------------------------------------------------------------
foreach cname : classics_shader_ported
    executable(
        cname + '_gles3',
        files(common_gles3_sources + [
            'src/gles3_harness.c',
            'src/classics_shader/cs_' + cname + '.c',
        ] + classics_shader_extras.get(cname, [])) + gen_sources,
        include_directories: inc,
        dependencies: common_gles3_deps,
        c_args: gles3_native_c_args + [
            '-DHACK_TABLE=@0@_xscreensaver_function_table'.format(cname),
        ],
        install: ncz_ship_bins.contains(cname + '_gles3'), install_dir: ncz_libdir,
    )
endforeach
