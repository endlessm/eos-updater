# Copyright 2020 Endless OS Foundation, LLC
# SPDX-License-Identifier: LGPL-2.1-or-later

# Python program
install_data('eos-updater-prepare-volume',
  install_dir: bindir,
  install_mode: 'rwxr-xr-x',
)

# lint check
test(
  'flake8',
  flake8,
  args: [files('eos-updater-prepare-volume')],
  suite: 'eos-updater-prepare-volume',
  workdir: meson.current_source_dir(),
)

# Documentation
install_man('docs/eos-updater-prepare-volume.8')
