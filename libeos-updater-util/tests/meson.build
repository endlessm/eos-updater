# Copyright 2020 Endless OS Foundation, LLC
# SPDX-License-Identifier: LGPL-2.1-or-later

deps = [
  dependency('flatpak', version: '>= 1.1.2'),
  dependency('gio-2.0', version: '>= 2.60.1'),
  dependency('glib-2.0', version: '>= 2.62.0'),
  dependency('gobject-2.0', version: '>= 2.44'),
  dependency('json-glib-1.0', version: '>= 1.2.6'),
  dependency('ostree-1', version: '>= 2019.2'),
  dependency('libsoup-2.4'),
  libeos_updater_util_dep,
]

c_args = [
  '-DG_LOG_DOMAIN="libeos-updater-util-tests"',
]

envs = test_env + [
  'G_TEST_SRCDIR=' + meson.current_source_dir(),
  'G_TEST_BUILDDIR=' + meson.current_build_dir(),
]

config_resources = gnome.compile_resources(
  'resources',
  'config.gresource.xml',
  c_name: 'euu_tests_resources',
)

test_programs = {
  'avahi-service-file': {},
  'config-util': {
    'source': ['config-util.c'] + config_resources,
  },
  'flatpak-util': {},
  'ostree-util': {},
}

installed_tests_metadir = join_paths(datadir, 'installed-tests',
                                     'libeos-updater-util-' + euu_api_version)
installed_tests_execdir = join_paths(libexecdir, 'installed-tests',
                                     'libeos-updater-util-' + euu_api_version)

foreach test_name, extra_args : test_programs
  source = extra_args.get('source', test_name + '.c')
  install = enable_installed_tests and extra_args.get('install', true)

  if install
    test_conf = configuration_data()
    test_conf.set('installed_tests_dir', installed_tests_execdir)
    test_conf.set('program', test_name)
    test_conf.set('env', '')
    configure_file(
      input: installed_tests_template,
      output: test_name + '.test',
      install_dir: installed_tests_metadir,
      configuration: test_conf,
    )
  endif

  exe = executable(test_name, source,
    c_args : c_args + extra_args.get('c_args', []),
    link_args : extra_args.get('link_args', []),
    dependencies : deps + extra_args.get('dependencies', []),
    install_dir: installed_tests_execdir,
    install: install,
  )

  suite = ['libeos-updater-util'] + extra_args.get('suite', [])
  test(test_name, exe, env : envs, suite : suite)
endforeach
