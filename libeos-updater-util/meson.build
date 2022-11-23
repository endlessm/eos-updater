# Copyright 2020, 2022 Endless OS Foundation, LLC
# SPDX-License-Identifier: LGPL-2.1-or-later

# Utility library
euu_api_version = '0'

libeos_updater_util_sources = [
  'avahi-service-file.c',
  'config-util.c',
  'flatpak-util.c',
  'ostree-bloom.c',
  'ostree-util.c',
  'types.c',
  'util.c',
]

libeos_updater_util_headers = [
  'avahi-service-file.h',
  'config-util.h',
  'flatpak-util.h',
  'ostree-util.h',
  'types.h',
  'util.h',
]

libeos_updater_util_private_headers = [
  'metrics-private.h',
  'ostree-bloom-private.h',
]

libeos_updater_util_cppflags = [
  '-DG_LOG_DOMAIN="libeos-updater-util"',
]

libeos_updater_util_deps = [
  dependency('flatpak', version: '>= 1.1.2'),
  dependency('gio-2.0', version: '>= 2.62'),
  dependency('glib-2.0', version: '>= 2.62'),
  dependency('gobject-2.0', version: '>= 2.62'),
  dependency('json-glib-1.0', version: '>= 1.2.6'),
  dependency('libsoup-2.4'),
  dependency('ostree-1', version: '>= 2019.2'),
]

enums = gnome.mkenums_simple('enums',
  sources: libeos_updater_util_headers,
  install_header: false,
)

libeos_updater_util = library('eos-updater-util-' + euu_api_version,
  libeos_updater_util_sources + libeos_updater_util_headers + libeos_updater_util_private_headers + enums,
  c_args: libeos_updater_util_cppflags,
  dependencies: libeos_updater_util_deps,
  include_directories: root_inc,
  install: true,
  # FIXME: Library versioning should be tidied up when ABI is next broken:
  soversion: euu_api_version,
  version: '0.0.0',
)
libeos_updater_util_dep = declare_dependency(
  link_with: libeos_updater_util,
  include_directories: root_inc,
  sources: libeos_updater_util_headers + [enums[1]],
)

libeos_updater_util_gir = gnome.generate_gir(libeos_updater_util,
  sources: libeos_updater_util_sources + libeos_updater_util_headers + enums,
  nsversion: euu_api_version,
  namespace: 'EosUpdaterUtil',
  symbol_prefix: 'euu',
  identifier_prefix: 'Euu',
  extra_args: [
    '--symbol-prefix=eos_updater',
    '--symbol-prefix=eos_ostree_avahi',
    '--symbol-prefix=eos_avahi',
    '--identifier-prefix=EosUpdater',
    '--identifier-prefix=EosOstreeAvahi',
    '--identifier-prefix=EosAvahi',
  ],
  header: 'libeos-updater-util/util.h',
  includes: [
    'Flatpak-1.0',
    'Gio-2.0',
    'GObject-2.0',
    'OSTree-1.0',
  ],
  install: true,
  dependencies: libeos_updater_util_dep,
)

subdir('tests')
