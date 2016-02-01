eos-updater
===========

System component of the OSTree based updater

This repo contains the automatic update tool eos-autoupdater and the
eos-updater daemon.  eos-autoupdater is run by a systemd timer, and once
running, communicates with eos-updater to initiate as much of the update
process as is set to be automatic.  Once that part of the process is
complete, it exits.

The user interface component will also monitor eos-updater's state and
prompt the user once the automatic part of the update has completed.
