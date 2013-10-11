eos-updater
===========

System component of the OSTree based updater

Currently, this repo contains only the automatic update tool eos-
updater.  It is run by a systemd timer, and once running, it
communicates with the OSTree daemon to initiate as much of the update
process as is set to be automatic.  Once that part of the process is
complete, it exits.

The user interface component will also monitor the OSTree daemon state
and prompt the user once the automatic part of the update has
completed.
