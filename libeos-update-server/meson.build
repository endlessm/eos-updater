# Server utility library
eus_api_version = '0'

resources = gnome.compile_resources(
  'resources',
  'libeos-update-server.gresource.xml',
  c_name: 'eus_resources',
  source_dir: meson.source_root(),
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
  dependency('gio-2.0', version: '>= 2.62'),
  dependency('glib-2.0', version: '>= 2.62'),
  dependency('gobject-2.0', version: '>= 2.62'),
  dependency('libsoup-2.4'),
  dependency('libsystemd'),
  dependency('ostree-1', version: '>= 2018.6'),
  libeos_updater_util_dep,
]

libeos_update_server = static_library('eos-update-server-' + eus_api_version,
  libeos_update_server_sources + libeos_update_server_headers + resources,
  c_args: libeos_update_server_cppflags,
  dependencies: libeos_update_server_deps,
  include_directories: root_inc,
  install: false,
  version: meson.project_version(),
)
libeos_update_server_dep = declare_dependency(
  link_with: libeos_update_server,
  include_directories: root_inc,
  sources: libeos_update_server_headers + [resources[1]],
)
