# NCZ-OS screensaver launcher, idle daemon and settings UX.
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

install_data('ncz-screensaver',
  install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x')
install_data('ncz-screensaver-settings',
  install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x')
install_data('ncz-screensaver-run-compat',
  rename: 'ncz-screensaver-run',
  install_dir: get_option('prefix') / get_option('libexecdir'),
  install_mode: 'rwxr-xr-x')
install_data('ncz-screensaver-idled.service',
  install_dir: get_option('prefix') / 'lib/systemd/user')
install_data('dev.ncz.screensaver.desktop',
  install_dir: get_option('datadir') / 'applications')

install_data('../config/dev.ncz.screensaver.gschema.xml',
  install_dir: get_option('datadir') / 'glib-2.0/schemas')
install_data('../config/hacks.tsv',
  install_dir: get_option('datadir') / 'ncz-screensavers')
# Compatibility path read by the existing Singularity screensaver-chooser plugin.
install_data('../config/hacks.tsv',
  install_dir: get_option('datadir') / 'ncz-screensaver-chooser')
