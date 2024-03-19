# Copyright 2022 Endless OS Foundation, LLC
# SPDX-License-Identifier: LGPL-2.1-or-later

install_data('eos_updater.py',
  install_dir : datadir / 'python-dbusmock' / 'templates',
  install_tag : 'devel',
)

# lint check
test(
  'flake8',
  flake8,
  args: [files('eos_updater.py')],
  suite: 'eos-updater',
  workdir: meson.current_source_dir(),
)
