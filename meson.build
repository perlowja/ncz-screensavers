# meson.build — native Wayland/EGL GL screensaver foundation.
#
# Round 1: builds a single executable `wl-screenhack` that links
# libwayland-client, libwayland-egl, EGL, and GLESv2. Protocol bindings
# (wayland.xml core, xdg-shell, wlr-layer-shell-unstable-v1) are
# generated at configure-time via wayland-scanner in private-code mode
# and compiled into the executable.

project(
    'ncz-wayland-screenhack',
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
# Dependencies — pkg-config only. No fallback to find_library; the build
# host is expected to have the development files for all four.
# ---------------------------------------------------------------------------

dep_wayland_client = dependency('wayland-client', required: true)
dep_wayland_egl    = dependency('wayland-egl',    required: true)
dep_egl            = dependency('egl',            required: true)
dep_glesv2         = dependency('glesv2',         required: true)

# ---------------------------------------------------------------------------
# wayland-scanner — generates both headers (client-side) and the private
# bindings code (no dlopen, all symbols static). One custom_target per
# protocol so build graphs are clean and incremental.
# ---------------------------------------------------------------------------

wayland_scanner = find_program('wayland-scanner', required: true)

# Common args for private-code generation (symbols are file-static).
# `client-header` emits the wl_* structs/listeners/enum definitions the
# client side uses; `private-code` emits the per-request dispatcher
# implementations with `static` linkage.
wayland_scanner_client_args  = ['client-header']
wayland_scanner_privcode_args = ['private-code']

# All protocol XMLs the executable depends on. wayland.xml is bundled
# so we don't depend on the system-installed copy.
protocols = {
    'wayland':           'protocols/wayland.xml',
    'xdg-shell':         'protocols/xdg-shell.xml',
    'wlr-layer-shell':   'protocols/wlr-layer-shell-unstable-v1.xml',
}

# Generated artifacts: one .h (header) and one .c (private code) per
# protocol. The .h is included from wl-screenhack.c; the .c is compiled
# into the executable. Both are passed to executable() via `sources:`
# so meson tracks the build order (headers must be generated before
# wl-screenhack.c is compiled).
gen_sources = []
foreach name, xml_path : protocols
    base = name.underscorify() + '-client-protocol'
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
# root is added so wl-screenhack.c can find them via relative path.
inc = include_directories('.', meson.current_build_dir())

# ---------------------------------------------------------------------------
# Executable
# ---------------------------------------------------------------------------

sources = files(
    'src/wl-screenhack.c',
) + gen_sources

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
    c_args: [
        '-Wall',
        '-Wextra',
    ],
    # Generated C includes its own definitions; warn if any are missing.
    install: false,                   # packaging policy comes later
)

# ---------------------------------------------------------------------------
# Convenience: 'meson compile -C build' will build, 'meson test -C build'
# has no tests in round 1, but we leave a placeholder message.
# ---------------------------------------------------------------------------
message('Configured wl-screenhack OK. Run `meson compile -C build` to build.')