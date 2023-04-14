# Copyright 2020 Endless OS Foundation, LLC
# SPDX-License-Identifier: LGPL-2.1-or-later

# Test helper library
libeos_updater_test_common_sources = [
  'convenience.c',
  'flatpak-spawn.c',
  'gpg.c',
  'httpd.c',
  'misc-utils.c',
  'ostree-spawn.c',
  'spawn-utils.c',
  'utils.c',
]

libeos_updater_test_common_headers = [
  'convenience.h',
  'flatpak-spawn.h',
  'gpg.h',
  'httpd.h',
  'misc-utils.h',
  'ostree-spawn.h',
  'spawn-utils.h',
  'utils.h',
]

libeos_updater_test_common_cppflags = [
  '-DFLATPAK_BINARY="@0@"'.format(find_program('flatpak').full_path()),
  '-DG_LOG_DOMAIN="eos-updater-test-common"',
  '-DGPG_BINARY="@0@"'.format(find_program('gpg').full_path()),
  '-DOSTREE_BINARY="@0@"'.format(find_program('ostree').full_path()),
  '-DOSTREE_WITH_AUTOCLEANUPS',
]

libeos_updater_test_common_deps = [
  dependency('gio-2.0', version: '>= 2.62'),
  dependency('gio-unix-2.0', version: '>= 2.62'),
  dependency('glib-2.0', version: '>= 2.70'),
  dependency('gobject-2.0', version: '>= 2.62'),
  dependency('libsoup-2.4', version: '>= 2.52'),
  dependency('ostree-1', version: '>= 2019.2'),
  libeos_updater_util_dep,
]

libeos_updater_test_common = static_library('eos-updater-test-common',
  libeos_updater_test_common_sources + libeos_updater_test_common_headers,
  c_args: libeos_updater_test_common_cppflags,
  dependencies: libeos_updater_test_common_deps,
  include_directories: root_inc,
  install: false,
)
libeos_updater_test_common_dep = declare_dependency(
  link_with: libeos_updater_test_common,
  include_directories: root_inc,
  sources: libeos_updater_test_common_headers,
)
