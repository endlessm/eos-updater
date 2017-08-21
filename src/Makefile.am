# Copyright © 2013 Vivek Dasmohapatra <vivek@collabora.com>
# Copyright © 2015 Dan Nicholson <nicholson@endlessm.com>
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

EXTRA_DIST =
dist_man8_MANS =

dbuslib = libeos-updater-dbus.la
preparelib = libeos-updater-0.la

noinst_LTLIBRARIES = $(dbuslib)
lib_LTLIBRARIES = $(preparelib)
libexec_PROGRAMS = eos-autoupdater
dist_bin_SCRIPTS = eos-updater-ctl

dist_man8_MANS += docs/eos-updater-ctl.8

BUILT_SOURCES = \
	eos-updater-generated.h \
	eos-updater-generated.c \
	$(NULL)

libeos_updater_dbus_la_CPPFLAGS = $(common_cppflags) -DG_LOG_DOMAIN=\""eos-updater-dbus"\"
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
	-include "config.h" \
	-DOSTREE_WITH_AUTOCLEANUPS \
	-DSYSCONFDIR=\""$(sysconfdir)"\" \
	-DLOCALSTATEDIR=\""$(localstatedir)"\" \
	-DDATADIR=\""$(datadir)"\" \
	-DPKGDATADIR=\""$(pkgdatadir)"\" \
	-DPREFIX=\""$(prefix)"\" \
	-DG_LOG_USE_STRUCTURED \
	$(NULL)

eos_autoupdater_CPPFLAGS = $(common_cppflags) -DG_LOG_DOMAIN=\""eos-autoupdater"\"
eos_autoupdater_CFLAGS = $(common_cflags) $(EOS_AUTOUPDATER_CFLAGS)
eos_autoupdater_LDFLAGS = $(WARN_LDFLAGS)
eos_autoupdater_LDADD = $(CODE_COVERAGE_LIBS) $(GIO_LIBS) $(dbuslib) $(top_builddir)/libeos-updater-util/libeos-updater-util-@EUU_API_VERSION@.la $(EOS_AUTOUPDATER_LIBS)
eos_autoupdater_SOURCES = eos-autoupdater.c
nodist_eos_autoupdater_SOURCES = $(eos_updater_resource_sources)

dist_man8_MANS += docs/eos-autoupdater.8

common_ldadd = $(CODE_COVERAGE_LIBS) $(GIO_LIBS) $(SOUP_LIBS) $(OSTREE_LIBS) $(top_builddir)/libeos-updater-util/libeos-updater-util-@EUU_API_VERSION@.la

libeos_updater_0_la_CPPFLAGS = $(common_cppflags) -DG_LOG_DOMAIN=\""libeos-updater"\"
libeos_updater_0_la_CFLAGS = $(common_cflags)
libeos_updater_0_la_LDFLAGS = $(WARN_LDFLAGS)
libeos_updater_0_la_LIBADD = $(common_ldadd)
libeos_updater_0_la_SOURCES = \
	eos-prepare-usb-update.c \
	eos-prepare-usb-update.h \
	$(NULL)

eosincludedir = $(includedir)/eos-updater-0
eosinclude_HEADERS = \
	eos-prepare-usb-update.h \
	$(NULL)

bin_PROGRAMS = eos-updater eos-updater-prepare-volume

eos_updater_prepare_volume_CPPFLAGS = \
	$(common_cppflags) \
	-DG_LOG_DOMAIN=\""eos-updater-prepare-volume"\" \
	$(NULL)
eos_updater_prepare_volume_CFLAGS = \
	$(common_cflags) \
	$(NULL)
eos_updater_prepare_volume_LDFLAGS = $(WARN_LDFLAGS)
eos_updater_prepare_volume_LDADD = \
	$(common_ldadd) \
	$(preparelib) \
	$(NULL)
eos_updater_prepare_volume_SOURCES = \
	eos-updater-prepare-volume.c \
	$(NULL)

dist_man8_MANS += docs/eos-updater-prepare-volume.8

eos_updater_CPPFLAGS = \
	$(common_cppflags) \
	-DG_LOG_DOMAIN=\""eos-updater"\" \
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
	$(NULL)
nodist_eos_updater_SOURCES = \
	$(eos_updater_resource_sources) \
	$(NULL)

dist_man8_MANS += docs/eos-updater.8

dbusconfdir = $(datadir)/dbus-1/system.d
dbussystemservicedir = $(datadir)/dbus-1/system-services
dbusinterfacesdir = $(datadir)/dbus-1/interfaces

dist_dbusconf_DATA = $(srcdir)/com.endlessm.Updater.conf
dist_dbussystemservice_DATA = $(srcdir)/com.endlessm.Updater.service
dist_dbusinterfaces_DATA = $(srcdir)/com.endlessm.Updater.xml

CLEANFILES = $(BUILT_SOURCES)

eos-updater-generated.c: eos-updater-generated.h
eos-updater-generated.h: com.endlessm.Updater.xml
	$(AM_V_GEN)$(GDBUS_CODEGEN)                \
	   --interface-prefix com.endlessm.        \
	   --generate-c-code eos-updater-generated \
	   --c-namespace Eos                       \
	   --c-generate-object-manager             \
	   --c-generate-autocleanup all            \
	   $<

# Resource handling.
eos_updater_resource_sources = \
	resources.c \
	resources.h \
	$(NULL)
eos_updater_resource_deps = $(shell glib-compile-resources --sourcedir=$(top_srcdir)/src --generate-dependencies $(top_srcdir)/src/eos-updater.gresource.xml)
resources.c: eos-updater.gresource.xml $(eos_updater_resource_deps)
	$(AM_V_GEN) $(GLIB_COMPILE_RESOURCES) \
		--c-name=eos_updater_resources --generate-source --internal \
		--sourcedir=$(top_srcdir)/src --target=$@ $<
resources.h: eos-updater.gresource.xml $(eos_updater_resource_deps)
	$(AM_V_GEN) $(GLIB_COMPILE_RESOURCES) \
		--c-name=eos_updater_resources --generate-header --internal \
		--sourcedir=$(top_srcdir)/src --target=$@ $<

BUILT_SOURCES += $(eos_updater_resource_sources)
CLEANFILES += $(eos_updater_resource_sources)
EXTRA_DIST += \
	eos-updater.gresource.xml \
	$(eos_updater_resource_deps) \
	$(NULL)

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
