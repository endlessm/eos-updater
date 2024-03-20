# Copyright 2020 Endless OS Foundation, LLC
# SPDX-License-Identifier: LGPL-2.1-or-later

eos_update_flatpak_installer_sources = [
  'main.c',
]

eos_update_flatpak_installer_deps = [
  flatpak_dep,
  gio_dep,
  glib_dep,
  gobject_dep,
  json_glib_dep,
  ostree_dep,
  libeos_updater_flatpak_installer_dep,
  libeos_updater_util_dep,
  eosmetrics_dep,
]

eos_update_flatpak_installer_cppflags = [
  '-DG_LOG_DOMAIN="eos-updater-flatpak-installer"',
]


executable('eos-updater-flatpak-installer',
  eos_update_flatpak_installer_sources,
  c_args: eos_update_flatpak_installer_cppflags,
  dependencies: eos_update_flatpak_installer_deps,
  include_directories: root_inc,
  install: true,
  install_dir: get_option('libexecdir'),
)

# systemd files
config = configuration_data()
config.set('libexecdir', join_paths(get_option('prefix'), get_option('libexecdir')))

configure_file(
  input: 'eos-updater-flatpak-installer.service.in',
  output: 'eos-updater-flatpak-installer.service',
  install_dir: systemd_dep.get_variable('systemdsystemunitdir'),
  configuration: config,
)

configure_file(
  input: 'eos-updater-flatpak-installer-fallback.service.in',
  output: 'eos-updater-flatpak-installer-fallback.service',
  install_dir: systemd_dep.get_variable('systemdsystemunitdir'),
  configuration: config,
)

configure_file(
  input: 'eos-updater-flatpak-installer-fallback.timer.in',
  output: 'eos-updater-flatpak-installer-fallback.timer',
  install_dir: systemd_dep.get_variable('systemdsystemunitdir'),
  configuration: config,
)

# JSON Schema file
install_data(
  files('eos-updater-autoinstall.schema.json'),
  install_dir: join_paths(get_option('prefix'), get_option('datadir'), 'eos-updater', 'schemas'),
)

# Documentation
install_man('docs/eos-updater-flatpak-autoinstall.d.5')
install_man('docs/eos-updater-flatpak-installer.8')
