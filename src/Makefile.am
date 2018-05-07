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

noinst_LTLIBRARIES = $(dbuslib)

BUILT_SOURCES = \
	eos-updater-generated.h \
	eos-updater-generated.c \
	$(NULL)

libeos_updater_dbus_la_CPPFLAGS = $(common_cppflags) -DG_LOG_DOMAIN=\""eos-updater-dbus"\"
libeos_updater_dbus_la_CFLAGS = \
	$(AM_CFLAGS) \
	$(STD_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS) \
	$(WARN_CFLAGS) \
	$(GIO_CFLAGS) \
	$(NULL)
libeos_updater_dbus_la_LIBADD = $(CODE_COVERAGE_LIBS) $(GIO_LIBS)
libeos_updater_dbus_la_SOURCES = \
	eos-updater-object.c \
	eos-updater-object.h \
	$(NULL)
nodist_libeos_updater_dbus_la_SOURCES = $(BUILT_SOURCES)

common_cflags = \
	$(AM_CFLAGS) \
	$(STD_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS) \
	$(WARN_CFLAGS) \
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

common_ldadd = $(CODE_COVERAGE_LIBS) $(GIO_LIBS) $(SOUP_LIBS) $(OSTREE_LIBS) $(top_builddir)/libeos-updater-util/libeos-updater-util-@EUU_API_VERSION@.la

bin_PROGRAMS = eos-updater

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

-include $(top_srcdir)/git.mk
