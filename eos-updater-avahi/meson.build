# Copyright 2020 Endless OS Foundation, LLC
# SPDX-License-Identifier: LGPL-2.1-or-later

eos_updater_avahi_sources = [
  'eos-updater-avahi.c',
]

eos_updater_avahi_deps = [
  gio_dep,
  glib_dep,
  gobject_dep,
  ostree_dep,
  libeos_update_server_dep,
  libeos_updater_util_dep,
]

eos_updater_avahi_cppflags = [
  '-DG_LOG_DOMAIN="eos-updater-avahi"',
]

executable('eos-updater-avahi',
  eos_updater_avahi_sources,
  c_args: eos_updater_avahi_cppflags,
  dependencies: eos_updater_avahi_deps,
  include_directories: root_inc,
  install: true,
  install_dir: get_option('libexecdir'),
)

# systemd files
config = configuration_data()
config.set('datadir', join_paths(get_option('prefix'), get_option('datadir')))
config.set('libexecdir', join_paths(get_option('prefix'), get_option('libexecdir')))
config.set('prefix', get_option('prefix'))
config.set('sysconfdir', join_paths(get_option('prefix'), get_option('sysconfdir')))

configure_file(
  input: 'eos-updater-avahi.path.in',
  output: 'eos-updater-avahi.path',
  install_dir: systemd_dep.get_variable('systemdsystemunitdir'),
  configuration: config,
)
configure_file(
  input: 'eos-updater-avahi.service.in',
  output: 'eos-updater-avahi.service',
  install_dir: systemd_dep.get_variable('systemdsystemunitdir'),
  configuration: config,
)

# Documentation
install_man('docs/eos-updater-avahi.8')
