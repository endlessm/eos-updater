# Copyright 2020 Endless OS Foundation, LLC
# SPDX-License-Identifier: LGPL-2.1-or-later

deps = [
  gio_dep,
  glib_dep,
  gobject_dep,
  ostree_dep,
  libeos_updater_test_common_dep,
  libeos_updater_util_dep,
]

c_args = [
  '-DG_LOG_DOMAIN="eos-updater-tests"',
]

envs = test_env + [
  'G_TEST_SRCDIR=' + meson.current_source_dir(),
  'G_TEST_BUILDDIR=' + meson.current_build_dir(),
  'OSTREE_SYSROOT_DEBUG=mutable-deployments',
]

# FIXME: Ensure these tests can be run installed as well.
# FIXME: None of them support running in parallel due to shared server resources; fix that.
test_programs = {
  'test-update-from-main': {
    'install': false,
    'parallel': false,
  },
  'test-update-from-lan': {
    'install': false,
    'parallel': false,
  },
  'test-update-from-volume': {
    'install': false,
    'parallel': false,
  },
  'test-update-cleanup-workaround': {
    'install': false,
    'parallel': false,
  },
  'test-update-broken-delta': {
    'install': false,
    'parallel': false,
  },
  'test-update-install-flatpaks': {
    'install': false,
    'parallel': false,
    'slow': true,
    'dependencies': [
      flatpak_dep,
      json_glib_dep,
    ],
  },
  'test-update-missing-deployed-commit': {
    'install': false,
    'parallel': false,
  },
  'test-update-refspec-checkpoint': {
    'install': false,
    'parallel': false,
    'slow': true,
  },
  'test-update-refspec-endoflife': {
    'install': false,
    'parallel': false,
  },
  'test-update-direct': {
    'install': false,
    'parallel': false,
    'dependencies': [
      libeos_updater_dbus_dep,
    ],
  },
  'test-autoupdater': {
    'install': false,
    'parallel': false,
  },
}

installed_tests_metadir = join_paths(datadir, 'installed-tests',
                                     'eos-updater-' + eu_api_version)
installed_tests_execdir = join_paths(libexecdir, 'installed-tests',
                                     'eos-updater-' + eu_api_version)

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

  test(
    test_name,
    exe,
    env: envs,
    suite: ['eos-updater'] + extra_args.get('suite', []),
    timeout: extra_args.get('slow', false) ? 600 : 60,
    is_parallel: extra_args.get('parallel', true),
    protocol: 'tap',
  )
endforeach

python_tests = {
  'eos-updater-avahi.py': {
    'installed_only': true,
  },
  'eos-update-server.py': {
    'installed_only': true,
  },
}

foreach test_name, extra_args : python_tests
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

  main = files(test_name)
  if install
    install_data(
      main,
      files('taptestrunner.py'),
      install_dir: installed_tests_execdir,
      install_mode: 'rwxr-xr-x',
    )
  endif

  if not extra_args.get('installed_only', false)
    test(
      test_name,
      py3,
      args: main,
      env: envs,
      suite: ['eos-updater'] + extra_args.get('suite', []),
      protocol: 'tap',
    )
  endif
endforeach

reuse = find_program('reuse', required: false, disabler: true)
test(
  'reuse',
  reuse,
  args: ['lint'],
  protocol: 'exitcode',
  suite: 'eos-updater',
  workdir: meson.project_source_root(),
)

subdir('services')
