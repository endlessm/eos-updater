# Copyright 2020 Endless OS Foundation, LLC
# SPDX-License-Identifier: LGPL-2.1-or-later

project('eos-updater', 'c',
  version : '1.4.0',
  meson_version : '>= 0.60.0',
  license: ['LGPL-2.1-or-later'],
  default_options : [
    'buildtype=debugoptimized',
    'warning_level=2',
    'c_std=gnu11',
  ]
)

gnome = import('gnome')
cc = meson.get_compiler('c')
py3_mod = import('python')
py3 = py3_mod.find_installation(
  'python3',
)
flake8 = find_program(
  'flake8',
  required : false,
  disabler : true,
)

prefix = get_option('prefix')
bindir = join_paths(prefix, get_option('bindir'))
datadir = join_paths(prefix, get_option('datadir'))
includedir = join_paths(prefix, get_option('includedir'))
libdir = join_paths(prefix, get_option('libdir'))
libexecdir = join_paths(prefix, get_option('libexecdir'))
localstatedir = join_paths(prefix, get_option('localstatedir'))
sysconfdir = join_paths(prefix, get_option('sysconfdir'))
sysconfexampledir = join_paths(prefix, get_option('datadir'), 'eos-updater')

dbus = dependency('dbus-1')
dbusconfdir = join_paths(datadir, 'dbus-1', 'system.d')
dbussystemservicedir = dbus.get_variable('system_bus_services_dir',
  pkgconfig_define: ['datadir', datadir])
dbusinterfacesdir = dbus.get_variable('interfaces_dir',
  pkgconfig_define: ['datadir', datadir])

# Common dependencies
avahi_client_dep = dependency('avahi-client', version: '>= 0.6.31')
avahi_glib_dep = dependency('avahi-glib', version: '>= 0.6.31')
eosmetrics_dep = dependency('eosmetrics-0', required: get_option('metrics'))
flatpak_dep = dependency('flatpak', version: '>= 1.1.2')
glib_dep = dependency('glib-2.0', version: '>= 2.70')
gio_dep = dependency('gio-2.0', version: '>= 2.62')
gio_unix_dep = dependency('gio-unix-2.0', version: '>= 2.62')
gobject_dep = dependency('gobject-2.0', version: '>= 2.62')
json_glib_dep = dependency('json-glib-1.0', version: '>= 1.2.6')
libnm_dep = dependency('libnm', version: '>= 1.2.0')
libsoup_dep = dependency('libsoup-3.0', version: '>= 3.2')
libsystemd_dep = dependency('libsystemd')
mogwai_dep = dependency('mogwai-schedule-client-0')
ostree_dep = dependency('ostree-1', version: '>= 2019.2')
systemd_dep = dependency('systemd')

config_h = configuration_data()
config_h.set('EOS_AVAHI_PORT', get_option('server_port'))
config_h.set_quoted('GETTEXT_PACKAGE', 'eos-updater')
config_h.set_quoted('PACKAGE_LOCALE_DIR', join_paths(get_option('prefix'), get_option('localedir')))
config_h.set_quoted('VERSION', meson.project_version())
config_h.set('HAVE_OSTREE_COMMIT_GET_OBJECT_SIZES', cc.has_function('ostree_commit_get_object_sizes', dependencies: [ostree_dep]))
config_h.set('HAS_EOSMETRICS_0', eosmetrics_dep.found())
configure_file(
  output: 'config.h',
  configuration: config_h,
)
root_inc = include_directories('.')

# Common compilation options
add_project_arguments(
  [
    '-DOSTREE_WITH_AUTOCLEANUPS',
    '-DSYSCONFDIR="@0@"'.format(sysconfdir),
    '-DLOCALSTATEDIR="@0@"'.format(localstatedir),
    '-DDATADIR="@0@"'.format(datadir),
    '-DPREFIX="@0@"'.format(prefix),
    '-DG_LOG_USE_STRUCTURED',
  ],
  language: 'c',
)

# Enable warning flags
test_c_args = [
  '-fno-strict-aliasing',
  '-fstack-protector-strong',
  '-Waggregate-return',
  '-Wall',
  '-Wunused',
  '-Warray-bounds',
  '-Wcast-align',
  '-Wclobbered',
  '-Wconversion',
  '-Wno-declaration-after-statement',
  '-Wdiscarded-qualifiers',
  '-Wduplicated-branches',
  '-Wduplicated-cond',
  '-Wempty-body',
  '-Wformat=2',
  '-Wformat-nonliteral',
  '-Wformat-security',
  '-Wformat-signedness',
  '-Wignored-qualifiers',
  '-Wimplicit-function-declaration',
  '-Wincompatible-pointer-types',
  '-Wincompatible-pointer-types-discards-qualifiers',
  '-Winit-self',
  '-Wint-conversion',
  '-Wlogical-op',
  '-Wmisleading-indentation',
  '-Wmissing-declarations',
  '-Wmissing-format-attribute',
  '-Wmissing-include-dirs',
  '-Wmissing-noreturn',
  '-Wmissing-parameter-type',
  '-Wmissing-prototypes',
  '-Wnested-externs',
  '-Wno-error=cpp',
  '-Wmissing-field-initializers',
  '-Wno-suggest-attribute=format',
  '-Wno-unused-parameter',
  '-Wnull-dereference',
  '-Wold-style-definition',
  '-Woverflow',
  '-Woverride-init',
  '-Wparentheses',
  '-Wpointer-arith',
  '-Wredundant-decls',
  '-Wreturn-type',
  '-Wshadow',
  '-Wsign-compare',
  '-Wstrict-aliasing=2',
  '-Wstrict-prototypes',
  '-Wswitch-default',
  '-Wswitch-enum',
  '-Wtype-limits',
  '-Wundef',
  '-Wuninitialized',
  '-Wunused-but-set-variable',
  '-Wunused-result',
  '-Wunused-variable',
  '-Wwrite-strings'
]
cc = meson.get_compiler('c')
add_project_arguments(cc.get_supported_arguments(test_c_args), language: 'c')

enable_installed_tests = get_option('installed_tests')
installed_tests_template = files('template.test.in')
test_env = [
  'G_DEBUG=gc-friendly,fatal-warnings',
  'MALLOC_CHECK_=2',
  'LC_ALL=C.UTF-8',
]

subdir('libeos-updater-util')
subdir('test-common')
subdir('libeos-update-server')
subdir('libeos-updater-flatpak-installer')
subdir('eos-updater')
subdir('eos-autoupdater')
subdir('eos-update-server')
subdir('eos-updater-avahi')
subdir('eos-updater-test-checkpoint')
subdir('eos-updater-ctl')
subdir('eos-updater-flatpak-installer')
subdir('eos-updater-prepare-volume')
subdir('tests')
