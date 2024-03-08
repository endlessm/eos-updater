# Copyright 2020 Endless OS Foundation, LLC
# SPDX-License-Identifier: LGPL-2.1-or-later

# Python program
install_data('eos-updater-ctl',
  install_dir: bindir,
  install_mode: 'rwxr-xr-x',
)

# lint check
test(
  'flake8',
  flake8,
  args: [files('eos-updater-ctl')],
  suite: 'eos-updater-ctl',
)

# Documentation
install_man('docs/eos-updater-ctl.8')
