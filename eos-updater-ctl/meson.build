# Python program
install_data('eos-updater-ctl',
  install_dir: bindir,
  install_mode: 'rwxr-xr-x',
)

# lint check
test(
  'flake8',
  py3,
  args: ['-m', 'flake8', files('eos-updater-ctl')],
  suite: 'eos-updater-ctl',
)

# Documentation
install_man('docs/eos-updater-ctl.8')
