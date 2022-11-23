# Copyright 2020 Endless OS Foundation, LLC
# SPDX-License-Identifier: LGPL-2.1-or-later

# Flatpak installer utility library
eufi_api_version = '0'

libeos_updater_flatpak_installer_sources = [
  'determine-flatpak-actions.c',
  'perform-flatpak-actions.c',
]

libeos_updater_flatpak_installer_headers = [
  'installer.h',
]

libeos_updater_flatpak_installer_cppflags = [
  '-DG_LOG_DOMAIN="libeos-updater-flatpak-installer"',
]

libeos_updater_flatpak_installer_deps = [
  dependency('flatpak', version: '>= 1.1.2'),
  dependency('gio-2.0', version: '>= 2.62'),
  dependency('glib-2.0', version: '>= 2.62'),
  dependency('gobject-2.0', version: '>= 2.62'),
  dependency('json-glib-1.0', version: '>= 1.2.6'),
  dependency('ostree-1', version: '>= 2019.2'),
  libeos_updater_util_dep,
]

libeos_updater_flatpak_installer = library('eos-updater-flatpak-installer-' + eufi_api_version,
  libeos_updater_flatpak_installer_sources + libeos_updater_flatpak_installer_headers,
  c_args: libeos_updater_flatpak_installer_cppflags,
  dependencies: libeos_updater_flatpak_installer_deps,
  include_directories: root_inc,
  install: true,
  # FIXME: Library versioning should be tidied up when ABI is next broken:
  soversion: eufi_api_version,
  version: '0.0.0',
)
libeos_updater_flatpak_installer_dep = declare_dependency(
  link_with: libeos_updater_flatpak_installer,
  include_directories: root_inc,
  sources: libeos_updater_flatpak_installer_headers,
)

libeos_updater_flatpak_installer_gir = gnome.generate_gir(libeos_updater_flatpak_installer,
  sources: libeos_updater_flatpak_installer_sources + libeos_updater_flatpak_installer_headers,
  nsversion: eufi_api_version,
  namespace: 'EosUpdaterFlatpakInstaller',
  symbol_prefix: 'eufi',
  identifier_prefix: 'Eufi',
  header: 'libeos-updater-flatpak-installer/installer.h',
  includes: [
    'Flatpak-1.0',
    'Gio-2.0',
    'GObject-2.0',
    libeos_updater_util_gir[0],
  ],
  install: true,
  dependencies: libeos_updater_flatpak_installer_dep,
)

subdir('tests')
