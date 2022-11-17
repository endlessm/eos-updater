# SPDX-License-Identifier: LGPL-2.1-or-later

eos_autoupdater_sources = [
  'main.c',
] + eos_updater_resources

eos_autoupdater_deps = [
  dependency('gio-2.0', version: '>= 2.62'),
  dependency('glib-2.0', version: '>= 2.62'),
  dependency('gobject-2.0', version: '>= 2.62'),
  dependency('libnm', version: '>= 1.2.0'),
  libeos_updater_dbus_dep,
  libeos_updater_util_dep,
]

eos_autoupdater_cppflags = [
  '-DG_LOG_DOMAIN="eos-autoupdater"',
]

executable('eos-autoupdater',
  eos_autoupdater_sources,
  c_args: eos_autoupdater_cppflags,
  dependencies: eos_autoupdater_deps,
  include_directories: root_inc,
  install: true,
  install_dir: get_option('libexecdir'),
)

# systemd files
config = configuration_data()
config.set('libexecdir', join_paths(get_option('prefix'), get_option('libexecdir')))

configure_file(
  input: 'eos-autoupdater.service.in',
  output: 'eos-autoupdater.service',
  install_dir: dependency('systemd').get_variable('systemdsystemunitdir'),
  configuration: config,
)
configure_file(
  input: 'eos-autoupdater.timer.in',
  output: 'eos-autoupdater.timer',
  install_dir: dependency('systemd').get_variable('systemdsystemunitdir'),
  configuration: config,
)

# Example configuration
install_data(
  files('eos-autoupdater.conf'),
  install_dir: sysconfexampledir,
)

# Documentation
install_man('docs/eos-autoupdater.conf.5')
install_man('docs/eos-autoupdater.8')
