# Test helper library
libeos_updater_test_common_sources = [
  'convenience.c',
  'flatpak-spawn.c',
  'gpg.c',
  'misc-utils.c',
  'ostree-spawn.c',
  'spawn-utils.c',
  'utils.c',
]

libeos_updater_test_common_headers = [
  'convenience.h',
  'flatpak-spawn.h',
  'gpg.h',
  'misc-utils.h',
  'ostree-spawn.h',
  'spawn-utils.h',
  'utils.h',
]

# ostree-trivial-httpd is in libexecdir/libostree or
# libexecdir/installed-tests/libostree if
# https://github.com/ostreedev/ostree/pull/1633 is merged.
# Unfortunately, but we don't know where ostree's libexecdir is. Try to
# build a path for typical places.
ostree_trivial_httpd = find_program(
    'ostree-trivial-httpd',
    join_paths(libexecdir, 'libostree/ostree-trivial-httpd'),
    join_paths(libexecdir, 'installed-tests/libostree/ostree-trivial-httpd'),
    '/usr/libexec/libostree/ostree-trivial-httpd',
    '/usr/libexec/installed-tests/libostree/ostree-trivial-httpd',
    '/usr/lib/libostree/ostree-trivial-httpd',
    '/usr/lib/installed-tests/libostree/ostree-trivial-httpd',
)

libeos_updater_test_common_cppflags = [
  '-DFLATPAK_BINARY="@0@"'.format(find_program('flatpak').path()),
  '-DG_LOG_DOMAIN="eos-updater-test-common"',
  '-DGPG_BINARY="@0@"'.format(find_program('gpg').path()),
  '-DOSTREE_BINARY="@0@"'.format(find_program('ostree').path()),
  '-DOSTREE_TRIVIAL_HTTPD_BINARY="@0@"'.format(ostree_trivial_httpd.path()),
  '-DOSTREE_WITH_AUTOCLEANUPS',
]

libeos_updater_test_common_deps = [
  dependency('gio-2.0', version: '>= 2.62'),
  dependency('gio-unix-2.0', version: '>= 2.62'),
  dependency('glib-2.0', version: '>= 2.62'),
  dependency('gobject-2.0', version: '>= 2.62'),
  dependency('ostree-1', version: '>= 2018.6'),
  libeos_updater_util_dep,
]

libeos_updater_test_common = static_library('eos-updater-test-common',
  libeos_updater_test_common_sources + libeos_updater_test_common_headers,
  c_args: libeos_updater_test_common_cppflags,
  dependencies: libeos_updater_test_common_deps,
  include_directories: root_inc,
  install: false,
  version: meson.project_version(),
)
libeos_updater_test_common_dep = declare_dependency(
  link_with: libeos_updater_test_common,
  include_directories: root_inc,
  sources: libeos_updater_test_common_headers,
)
