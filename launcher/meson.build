# NCZ-OS screensaver launcher and idle daemon. The settings UX is the Singularity
# plugin in plugin/; there is no standalone settings application.
# Included from the top-level meson.build with subdir('launcher'); it must not
# call project(). tools/launcher-meson-check.sh builds it from a scratch root.

wl_client_dep = dependency('wayland-client')
glib_dep = dependency('glib-2.0', version: '>=2.70')
gio_dep = dependency('gio-2.0')
gio_unix_dep = dependency('gio-unix-2.0')
wayland_scanner = find_program('wayland-scanner')

ncz_proto_dir = meson.project_source_root() / 'protocols'
ncz_proto_files = []
foreach p : ['ext-idle-notify-v1', 'wlr-output-power-management-unstable-v1']
  xml = ncz_proto_dir / p + '.xml'
  ncz_proto_files += custom_target(p + '-c',
    input: xml, output: p + '-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'])
  ncz_proto_files += custom_target(p + '-h',
    input: xml, output: p + '-client-protocol.h',
    command: [wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@'])
endforeach

executable('ncz-screensaver-idled',
  ['ncz-screensaver-idled.c', ncz_proto_files],
  dependencies: [wl_client_dep, glib_dep, gio_dep, gio_unix_dep],
  c_args: ['-D_GNU_SOURCE'],
  install: true,
  install_dir: get_option('prefix') / get_option('libexecdir'))

gl_deps = [dependency('egl'), dependency('glesv2'), dependency('dl'),
           meson.get_compiler('c').find_library('m')]
executable('ncz-screensaver-calibrate',
  ['ncz-screensaver-calibrate.c'],
  dependencies: gl_deps,
  c_args: ['-D_GNU_SOURCE'],
  install: true,
  install_dir: get_option('prefix') / get_option('libexecdir') / 'ncz-screensavers')

install_data('ncz-screensaver',
  install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x')
install_data('ncz-screensaver-run-compat',
  rename: 'ncz-screensaver-run',
  install_dir: get_option('prefix') / get_option('libexecdir'),
  install_mode: 'rwxr-xr-x')
install_data('ncz-screensaver-idled.service',
  install_dir: get_option('prefix') / 'lib/systemd/user')

# The schema and the compatibility catalog copy under
# share/ncz-screensaver-chooser are installed by the top-level meson.build.
install_data('../assets/screensaver-chooser/hacks.tsv',
  install_dir: get_option('datadir') / 'ncz-screensavers')
