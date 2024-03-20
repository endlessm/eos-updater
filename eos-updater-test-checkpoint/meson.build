# Copyright 2024 Endless OS Foundation, LLC
# SPDX-License-Identifier: LGPL-2.1-or-later

eos_updater_test_checkpoint_sources = [
  'main.c'
]

eos_updater_test_checkpoint_deps = [
  ostree_dep,
  libeos_updater_util_dep,
]

executable('eos-updater-test-checkpoint',
  eos_updater_test_checkpoint_sources,
  c_args: [
    '-DG_LOG_DOMAIN="eos-updater-test-checkpoint"'
  ],
  dependencies: eos_updater_test_checkpoint_deps,
  include_directories: root_inc,
  install: true,
  install_dir: get_option('libexecdir'),
)

install_man('docs/eos-updater-test-checkpoint.8')
