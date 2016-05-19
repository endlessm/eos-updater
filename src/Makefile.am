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

libexec_PROGRAMS = eos-autoupdater
bin_PROGRAMS = eos-updater
dist_bin_SCRIPTS = eos-updater-ctl

BUILT_SOURCES = \
	eos-updater-generated.h \
	eos-updater-generated.c \
	$(NULL)

eos_autoupdater_CFLAGS = $(EOS_AUTOUPDATER_CFLAGS)
eos_autoupdater_LDADD = $(EOS_AUTOUPDATER_LIBS)
eos_autoupdater_SOURCES = eos-autoupdater.c
nodist_eos_autoupdater_SOURCES = $(BUILT_SOURCES)

eos_updater_CFLAGS = $(EOS_UPDATER_CFLAGS)
eos_updater_LDADD  = $(EOS_UPDATER_LIBS)
eos_updater_SOURCES = \
	eos-updater-apply.c \
	eos-updater-apply.h \
	eos-updater-fetch.c \
	eos-updater-fetch.h \
	eos-updater-poll.c \
	eos-updater-poll.h \
	eos-updater-util.c \
	eos-updater-util.h \
	eos-updater-types.h \
	eos-updater.c \
	eos-updater.xml \
	eos-updater-data.h \
	eos-updater-data.c \
	$(NULL)
nodist_eos_updater_SOURCES = $(BUILT_SOURCES)

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
