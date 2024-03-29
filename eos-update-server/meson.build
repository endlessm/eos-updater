# Copyright 2020 Endless OS Foundation, LLC
# SPDX-License-Identifier: LGPL-2.1-or-later

eos_update_server_sources = [
  'eos-update-server.c',
]

eos_update_server_deps = [
  gio_dep,
  glib_dep,
  gobject_dep,
  libsoup_dep,
  libsystemd_dep,
  ostree_dep,
  libeos_update_server_dep,
  libeos_updater_util_dep,
]

eos_update_server_cppflags = [
  '-DG_LOG_DOMAIN="eos-update-server"',
]

executable('eos-update-server',
  eos_update_server_sources,
  c_args: eos_update_server_cppflags,
  dependencies: eos_update_server_deps,
  include_directories: root_inc,
  install: true,
  install_dir: get_option('libexecdir'),
)

# systemd files
config = configuration_data()
config.set('libexecdir', join_paths(get_option('prefix'), get_option('libexecdir')))
config.set('server_port', get_option('server_port'))
config.set('sysconfdir', join_paths(get_option('prefix'), get_option('sysconfdir')))

configure_file(
  input: 'eos-update-server.service.in',
  output: 'eos-update-server.service',
  install_dir: systemd_dep.get_variable('systemdsystemunitdir'),
  configuration: config,
)
configure_file(
  input: 'eos-update-server.socket.in',
  output: 'eos-update-server.socket',
  install_dir: systemd_dep.get_variable('systemdsystemunitdir'),
  configuration: config,
)

# Example configuration
install_data(
  files('eos-update-server.conf'),
  install_dir: sysconfexampledir,
)

# Documentation
install_man('docs/eos-update-server.conf.5')
install_man('docs/eos-update-server.8')
