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

dbuslib = libeos-updater-dbus.la
utillib = libeos-updater-util.la

noinst_LTLIBRARIES = $(dbuslib) $(utillib)
libexec_PROGRAMS = eos-autoupdater eos-update-server
dist_bin_SCRIPTS = eos-updater-ctl

BUILT_SOURCES = \
	eos-updater-generated.h \
	eos-updater-generated.c \
	$(NULL)

libeos_updater_dbus_la_CFLAGS = $(GIO_CFLAGS)
libeos_updater_dbus_la_LIBADD = $(GIO_LIBS)
libeos_updater_dbus_la_SOURCES = eos-updater-types.h
nodist_libeos_updater_dbus_la_SOURCES = $(BUILT_SOURCES)

libeos_updater_util_la_CFLAGS = $(GIO_CFLAGS) $(SOUP_CFLAGS) $(OSTREE_CFLAGS) -DOSTREE_WITH_AUTOCLEANUPS
libeos_updater_util_la_LIBADD = $(GIO_LIBS) $(SOUP_LIBS) $(OSTREE_LIBS) $(dbuslib)
libeos_updater_util_la_SOURCES = \
	eos-util.c \
	eos-util.h \
	eos-refcounted.h \
	$(NULL)

eos_autoupdater_CFLAGS = $(GIO_CFLAGS) $(EOS_AUTOUPDATER_CFLAGS)
eos_autoupdater_LDADD = $(GIO_LIBS) $(dbuslib) $(EOS_AUTOUPDATER_LIBS)
eos_autoupdater_SOURCES = eos-autoupdater.c

common_cflags = $(GIO_CFLAGS) $(SOUP_CFLAGS) $(OSTREE_CFLAGS) -DOSTREE_WITH_AUTOCLEANUPS
common_ldadd = $(GIO_LIBS) $(SOUP_LIBS) $(OSTREE_LIBS) $(utillib)

eos_update_server_CFLAGS = $(common_cflags) $(EOS_UPDATE_SERVER_CFLAGS)
eos_update_server_LDADD = $(common_ldadd) $(EOS_UPDATE_SERVER_LIBS)
eos_update_server_SOURCES = eos-update-server.c

bin_PROGRAMS = eos-updater

eos_updater_CFLAGS = \
	$(common_cflags) \
	$(EOS_UPDATER_CFLAGS) \
	$(NULL)
eos_updater_LDADD  = $(common_ldadd) $(EOS_UPDATER_LIBS)
eos_updater_SOURCES = \
	eos-updater-apply.c \
	eos-updater-apply.h \
	eos-updater-avahi-emulator.c \
	eos-updater-avahi-emulator.h \
	eos-updater-avahi.c \
	eos-updater-avahi.h \
	eos-updater-branch-file.h \
	eos-updater-branch-file.c \
	eos-updater-data.h \
	eos-updater-data.c \
	eos-updater-extensions.c \
	eos-updater-extensions.h \
	eos-updater-fetch.c \
	eos-updater-fetch.h \
	eos-updater-poll.c \
	eos-updater-poll.h \
	eos-updater-poll-common.h \
	eos-updater-poll-common.c \
	eos-updater-poll-lan.h \
	eos-updater-poll-lan.c \
	eos-updater-poll-main.h \
	eos-updater-poll-main.c \
	eos-updater.c \
	eos-updater.xml \
	$(NULL)

dbusconfdir = ${sysconfdir}/dbus-1/system.d
dbussystemservicedir = $(datadir)/dbus-1/system-services

dbusconf_DATA = $(eos_updater_dbus_if).conf
dbussystemservice_DATA = $(eos_updater_dbus_if).service

CLEANFILES = $(BUILT_SOURCES) $(dbusconf_DATA) $(dbussystemservice_DATA)

EXTRA_DIST = \
	eos-updater-ifname.xslt \
	eos-updater-policy.xslt \
	eos-updater-service.xslt \
	eos-updater.xml \
	$(NULL)

eos_updater_dbus_if := \
	$(shell $(XSLTPROC) $(srcdir)/eos-updater-ifname.xslt \
		$(srcdir)/eos-updater.xml)

eos-updater-generated.c: eos-updater-generated.h
eos-updater-generated.h: eos-updater.xml
	$(AM_V_GEN)$(GDBUS_CODEGEN)                        \
	   --interface-prefix $(basename $(eos_updater_dbus_if)). \
	   --generate-c-code eos-updater-generated	\
	   --c-namespace Eos                    \
	   --c-generate-object-manager          \
	   $<

$(eos_updater_dbus_if).conf: eos-updater-policy.xslt eos-updater.xml
	$(AM_V_GEN)$(XSLTPROC) $+ > $@

$(eos_updater_dbus_if).service: eos-updater-service.xslt eos-updater.xml
	$(AM_V_GEN)$(XSLTPROC) $+ > $@
