# Copyright 2020 Endless OS Foundation, LLC
# SPDX-License-Identifier: LGPL-2.1-or-later

# Service file used for some tests
config = configuration_data()
config.set('libexecdir', join_paths(get_option('prefix'), get_option('libexecdir')))

configure_file(
  input: 'org.freedesktop.Flatpak.SystemHelper.service.in',
  output: 'org.freedesktop.Flatpak.SystemHelper.service',
  install: false,
  configuration: config,
)
