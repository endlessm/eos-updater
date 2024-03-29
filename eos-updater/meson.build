# Copyright 2020, 2022 Endless OS Foundation, LLC
# SPDX-License-Identifier: LGPL-2.1-or-later

# D-Bus helper library
libeos_updater_dbus_codegen_sources = gnome.gdbus_codegen(
  'dbus',
  sources: 'com.endlessm.Updater.xml',
  interface_prefix: 'com.endlessm.',
  namespace: 'Eos',
  object_manager: true,
  autocleanup: 'all',
)

libeos_updater_dbus_sources = [
  'object.c',
]
libeos_updater_dbus_headers = [
  'object.h',
]

libeos_updater_dbus_cppflags = [
  '-DG_LOG_DOMAIN="eos-updater-dbus"',
]

libeos_updater_dbus_deps = [
  avahi_client_dep,
  avahi_glib_dep,
  eosmetrics_dep,
  flatpak_dep,
  gio_dep,
  gio_unix_dep,
  glib_dep,
  gobject_dep,
  mogwai_dep,
  ostree_dep,
  libeos_updater_util_dep,
]

libeos_updater_dbus = static_library('eos-updater-dbus',
  libeos_updater_dbus_sources + libeos_updater_dbus_headers + libeos_updater_dbus_codegen_sources,
  c_args: libeos_updater_dbus_cppflags,
  dependencies: libeos_updater_dbus_deps,
  include_directories: root_inc,
  install: false,
)
libeos_updater_dbus_dep = declare_dependency(
  link_with: libeos_updater_dbus,
  include_directories: root_inc,
  sources: libeos_updater_dbus_headers + [libeos_updater_dbus_codegen_sources[1]],
)

# eos-updater daemon
eu_api_version = '0'

eos_updater_resources = gnome.compile_resources(
  'resources',
  'eos-updater.gresource.xml',
  c_name: 'eos_updater_resources',
)

eos_updater_sources = [
  'apply.c',
  'apply.h',
  'data.c',
  'data.h',
  'fetch.c',
  'fetch.h',
  'live-boot.c',
  'live-boot.h',
  'main.c',
  'poll.c',
  'poll.h',
  'poll-common.c',
  'poll-common.h',
] + eos_updater_resources

eos_updater_deps = libeos_updater_dbus_deps + [libeos_updater_dbus_dep]

eos_updater_cppflags = [
  '-DG_LOG_DOMAIN="eos-updater"',
]

executable('eos-updater',
  eos_updater_sources,
  c_args: eos_updater_cppflags,
  dependencies: eos_updater_deps,
  include_directories: root_inc,
  install: true,
  install_dir: get_option('libexecdir'),
)

# D-Bus files
install_data(
  files('com.endlessm.Updater.conf'),
  install_dir: dbusconfdir,
)
install_data(
  files('com.endlessm.Updater.service'),
  install_dir: dbussystemservicedir,
)
install_data(
  files('com.endlessm.Updater.xml'),
  install_dir: dbusinterfacesdir,
)

# systemd files
config = configuration_data()
config.set('libexecdir', join_paths(get_option('prefix'), get_option('libexecdir')))
config.set('ostree', find_program('ostree').full_path())

configure_file(
  input: 'eos-updater.service.in',
  output: 'eos-updater.service',
  install_dir: systemd_dep.get_variable('systemdsystemunitdir'),
  configuration: config,
)

configure_file(
  input: 'eos-updater-autocleanup.service.in',
  output: 'eos-updater-autocleanup.service',
  install_dir: systemd_dep.get_variable('systemdsystemunitdir'),
  configuration: config,
)

configure_file(
  input: 'finalize-autocleanup.conf.in',
  output: 'autocleanup.conf',
  install_dir: join_paths(systemd_dep.get_variable('systemdsystemunitdir'), 'ostree-finalize-staged.service.d'),
  configuration: config,
)

# Example configuration
install_data(
  files('eos-updater.conf'),
  install_dir: sysconfexampledir,
)

# Documentation
install_man('docs/eos-updater.conf.5')
install_man('docs/eos-updater.8')

subdir('tests')
