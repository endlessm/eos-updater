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
preparelib = libeos-updater-0.la

noinst_LTLIBRARIES = $(dbuslib)
lib_LTLIBRARIES = $(preparelib)
libexec_PROGRAMS = eos-autoupdater eos-update-server
dist_bin_SCRIPTS = eos-updater-ctl

BUILT_SOURCES = \
	eos-updater-generated.h \
	eos-updater-generated.c \
	$(NULL)

libeos_updater_dbus_la_CPPFLAGS = $(common_cppflags)
libeos_updater_dbus_la_CFLAGS = $(WARN_CFLAGS) $(CODE_COVERAGE_CFLAGS) $(GIO_CFLAGS)
libeos_updater_dbus_la_LIBADD = $(CODE_COVERAGE_LIBS) $(GIO_LIBS)
libeos_updater_dbus_la_SOURCES = \
	eos-updater-object.c \
	eos-updater-object.h \
	eos-updater-types.c \
	eos-updater-types.h \
	$(NULL)
nodist_libeos_updater_dbus_la_SOURCES = $(BUILT_SOURCES)

common_cflags = \
	$(WARN_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS) \
	$(GIO_CFLAGS) \
	$(SOUP_CFLAGS) \
	$(OSTREE_CFLAGS) \
	$(NULL)
common_cppflags = \
	-I$(top_srcdir) \
	-I$(top_builddir) \
	-DOSTREE_WITH_AUTOCLEANUPS \
	-DSYSCONFDIR=\""$(sysconfdir)"\" \
	-DLOCALSTATEDIR=\""$(localstatedir)"\" \
	-DDATADIR=\""$(datadir)"\" \
	-DPKGDATADIR=\""$(pkgdatadir)"\" \
	-DPREFIX=\""$(prefix)"\" \
	$(NULL)

eos_autoupdater_CPPFLAGS = $(common_cppflags)
eos_autoupdater_CFLAGS = $(common_cflags) $(EOS_AUTOUPDATER_CFLAGS)
eos_autoupdater_LDFLAGS = $(WARN_LDFLAGS)
eos_autoupdater_LDADD = $(CODE_COVERAGE_LIBS) $(GIO_LIBS) $(dbuslib) $(top_builddir)/libeos-updater-util/libeos-updater-util-@EUU_API_VERSION@.la $(EOS_AUTOUPDATER_LIBS)
eos_autoupdater_SOURCES = eos-autoupdater.c

common_ldadd = $(CODE_COVERAGE_LIBS) $(GIO_LIBS) $(SOUP_LIBS) $(OSTREE_LIBS) $(top_builddir)/libeos-updater-util/libeos-updater-util-@EUU_API_VERSION@.la

libeos_updater_0_la_CPPFLAGS = $(common_cppflags)
libeos_updater_0_la_CFLAGS = $(common_cflags)
libeos_updater_0_la_LDFLAGS = $(WARN_LDFLAGS)
libeos_updater_0_la_LIBADD = $(common_ldadd)
libeos_updater_0_la_SOURCES = \
	eos-prepare-usb-update.c \
	eos-prepare-usb-update.h \
	eos-repo-server.c \
	eos-repo-server.h \
	$(NULL)

eosincludedir = $(includedir)/eos-updater-0
eosinclude_HEADERS = \
	eos-prepare-usb-update.h \
	eos-repo-server.h \
	$(NULL)

eos_update_server_CPPFLAGS = $(common_cppflags)
eos_update_server_CFLAGS = $(common_cflags) $(EOS_UPDATE_SERVER_CFLAGS)
eos_update_server_LDFLAGS = $(WARN_LDFLAGS)
eos_update_server_LDADD = $(common_ldadd) $(preparelib) $(EOS_UPDATE_SERVER_LIBS)
eos_update_server_SOURCES = eos-update-server.c

bin_PROGRAMS = eos-updater eos-prepare-volume

eos_prepare_volume_CPPFLAGS = \
	$(common_cppflags) \
	$(NULL)
eos_prepare_volume_CFLAGS = \
	$(common_cflags) \
	$(NULL)
eos_prepare_volume_LDFLAGS = $(WARN_LDFLAGS)
eos_prepare_volume_LDADD = \
	$(common_ldadd) \
	$(preparelib) \
	$(NULL)
eos_prepare_volume_SOURCES = \
	eos-prepare-volume.c \
	$(NULL)

eos_updater_CPPFLAGS = \
	$(common_cppflags) \
	$(NULL)
eos_updater_CFLAGS = \
	$(common_cflags) \
	$(EOS_UPDATER_CFLAGS) \
	$(NULL)
eos_updater_LDFLAGS = $(WARN_LDFLAGS)
eos_updater_LDADD  = $(common_ldadd) $(dbuslib) $(EOS_UPDATER_LIBS)
eos_updater_SOURCES = \
	eos-updater-apply.c \
	eos-updater-apply.h \
	eos-updater-avahi-emulator.c \
	eos-updater-avahi-emulator.h \
	eos-updater-avahi.c \
	eos-updater-avahi.h \
	eos-updater-data.h \
	eos-updater-data.c \
	eos-updater-fetch.c \
	eos-updater-fetch.h \
	eos-updater-live-boot.c \
	eos-updater-live-boot.h \
	eos-updater-poll.c \
	eos-updater-poll.h \
	eos-updater-poll-common.h \
	eos-updater-poll-common.c \
	eos-updater-poll-lan.h \
	eos-updater-poll-lan.c \
	eos-updater-poll-main.h \
	eos-updater-poll-main.c \
	eos-updater-poll-volume-dbus.c \
	eos-updater-poll-volume-dbus.h \
	eos-updater-poll-volume.c \
	eos-updater-poll-volume.h \
	eos-updater.c \
	eos-updater.xml \
	$(NULL)

dbusconfdir = $(datadir)/dbus-1/system.d
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



-include $(INTROSPECTION_MAKEFILE)
INTROSPECTION_GIRS =
INTROSPECTION_SCANNER_ARGS = --add-include-path=$(srcdir) --warn-all
INTROSPECTION_COMPILER_ARGS = --includedir=$(srcdir)

if HAVE_INTROSPECTION

EosUpdater-0.0.gir: $(preparelib)
EosUpdater_0_0_gir_INCLUDES = Soup-2.4 OSTree-1.0
EosUpdater_0_0_gir_LIBS = $(preparelib)
EosUpdater_0_0_gir_FILES = $(libeos_updater_0_la_SOURCES)
EosUpdater_0_0_gir_SCANNERFLAGS = $(WARN_SCANNERFLAGS)
INTROSPECTION_GIRS += EosUpdater-0.0.gir

girdir = $(datadir)/gir-1.0
gir_DATA = $(INTROSPECTION_GIRS)

typelibdir = $(libdir)/girepository-1.0
typelib_DATA = $(INTROSPECTION_GIRS:.gir=.typelib)

CLEANFILES += $(gir_DATA) $(typelib_DATA)

endif

-include $(top_srcdir)/git.mk
