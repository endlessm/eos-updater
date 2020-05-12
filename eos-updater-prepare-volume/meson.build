# Python program
install_data('eos-updater-prepare-volume',
  install_dir: bindir,
  install_mode: 'rwxr-xr-x',
)

# lint check
test(
  'flake8',
  py3,
  args: ['-m', 'flake8', files('eos-updater-prepare-volume')],
  suite: 'eos-updater-prepare-volume',
)

# Documentation
install_man('docs/eos-updater-prepare-volume.8')
