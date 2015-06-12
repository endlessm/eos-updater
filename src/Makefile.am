# Makefile for C source code
#
# Copyright © 2013 Vivek Dasmohapatra <vivek@collabora.com>
# Copyright © 2015 Dan Nicholson <nicholson@endlessm.com>
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the
# Free Software Foundation, Inc., 59 Temple Place - Suite 330,
# Boston, MA 02111-1307, USA.

libexec_PROGRAMS = eos-updater
bin_PROGRAMS = ostree-daemon
dist_bin_SCRIPTS = ostree-daemon-ctl

BUILT_SOURCES = \
	ostree-daemon-generated.h \
	ostree-daemon-generated.c \
	$(NULL)

eos_updater_CFLAGS = $(EOS_UPDATER_CFLAGS)
eos_updater_LDADD = $(EOS_UPDATER_LIBS)
eos_updater_SOURCES = eos-updater.c
nodist_eos_updater_SOURCES = $(BUILT_SOURCES)

ostree_daemon_CFLAGS = $(OSTREE_DAEMON_CFLAGS)
ostree_daemon_LDADD  = $(OSTREE_DAEMON_LIBS)
ostree_daemon_SOURCES = \
	ostree-daemon-apply.c \
	ostree-daemon-apply.h \
	ostree-daemon-fetch.c \
	ostree-daemon-fetch.h \
	ostree-daemon-poll.c \
	ostree-daemon-poll.h \
	ostree-daemon-util.c \
	ostree-daemon-util.h \
	ostree-daemon.c \
	ostree-daemon.h \
	ostree-daemon.xml \
	$(NULL)
nodist_ostree_daemon_SOURCES = $(BUILT_SOURCES)

dbusconfdir = ${sysconfdir}/dbus-1/system.d
dbussystemservicedir = $(datadir)/dbus-1/system-services

dbusconf_DATA = $(ostree_daemon_dbus_if).conf
dbussystemservice_DATA = $(ostree_daemon_dbus_if).service

CLEANFILES = $(BUILT_SOURCES) $(dbusconf_DATA) $(dbussystemservice_DATA)

EXTRA_DIST = \
	ostree-daemon-ifname.xslt \
	ostree-daemon-policy.xslt \
	ostree-daemon-service.xslt \
	ostree-daemon.xml \
	$(NULL)

ostree_daemon_dbus_if := \
	$(shell $(XSLTPROC) $(srcdir)/ostree-daemon-ifname.xslt \
		$(srcdir)/ostree-daemon.xml)

$(BUILT_SOURCES): ostree-daemon.xml
	$(GDBUS_CODEGEN)                        \
	   --interface-prefix $(basename $(ostree_daemon_dbus_if)). \
	   --generate-c-code $(basename $(@F))  \
	   --c-namespace OTD                    \
	   --c-generate-object-manager          \
	   $(<F)

$(ostree_daemon_dbus_if).conf: ostree-daemon-policy.xslt ostree-daemon.xml
	$(XSLTPROC) $+ > $@

$(ostree_daemon_dbus_if).service: ostree-daemon-service.xslt ostree-daemon.xml
	$(XSLTPROC) $+ > $@
