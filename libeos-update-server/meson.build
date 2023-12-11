# Copyright 2020 Endless OS Foundation, LLC
# SPDX-License-Identifier: LGPL-2.1-or-later

# Server utility library
eus_api_version = '0'

resources = gnome.compile_resources(
  'resources',
  'libeos-update-server.gresource.xml',
  c_name: 'eus_resources',
)

libeos_update_server_sources = [
  'config.c',
  'repo.c',
  'server.c',
]

libeos_update_server_headers = [
  'config.h',
  'repo.h',
  'server.h',
]

libeos_update_server_cppflags = [
  '-DG_LOG_DOMAIN="libeos-update-server"',
]

libeos_update_server_deps = [
  gio_dep,
  glib_dep,
  gobject_dep,
  libsoup_dep,
  libsystemd_dep,
  ostree_dep,
  libeos_updater_util_dep,
]

libeos_update_server = static_library('eos-update-server-' + eus_api_version,
  libeos_update_server_sources + libeos_update_server_headers + resources,
  c_args: libeos_update_server_cppflags,
  dependencies: libeos_update_server_deps,
  include_directories: root_inc,
  install: false,
)
libeos_update_server_dep = declare_dependency(
  link_with: libeos_update_server,
  include_directories: root_inc,
  sources: libeos_update_server_headers + [resources[1]],
)
