eos-updater
===========

Overview
--------

System component of the OSTree based updater

This repo contains the automatic update tool eos-autoupdater and the
eos-updater daemon.  eos-autoupdater is run by a systemd timer, and once
running, communicates with eos-updater to initiate as much of the update
process as is set to be automatic.  Once that part of the process is
complete, it exits.

The user interface component will also monitor eos-updater's state and
prompt the user once the automatic part of the update has completed.

Also included in the repository are eos-update-server and eos-updater-avahi,
which coordinate to advertise OSTree updates to computers on the local network.

For documentation about the updater, see the man pages for each program and
for the various configuration files it uses:
 • eos-autoupdater(8)
 • eos-update-server(8)
 • eos-updater(8)
 • eos-updater-avahi(8)
 • eos-updater-ctl(8)
 • eos-updater-prepare-volume(8)
 • eos-autoupdater.conf(5)
 • eos-update-server.conf(5)
 • eos-updater.conf(5)

Licensing
---------

eos-updater is licensed under the LGPL-2.1+.

Testing
-------

eos-updater comes with a number of unit and integration tests which can be run
using `meson test`; and some installed-only tests which can be run using
`gnome-desktop-testing-runner libeos-updater-util-0` and
`sudo gnome-desktop-testing-runner eos-updater-0` in your installation prefix
_after_ installing eos-updater.

Bug reports
-----------

Please file bug reports on https://support.endlessm.com/, and include the
output of `eos-diagnostics --verbose`.
