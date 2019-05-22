# Copyright © 2017 Endless Mobile, Inc.
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

ACLOCAL_AMFLAGS = ${ACLOCAL_FLAGS} -I m4

SUBDIRS = . libeos-updater-util/tests libeos-updater-flatpak-installer/tests tests

dbusconfdir = $(datadir)/dbus-1/system.d
dbussystemservicedir = $(datadir)/dbus-1/system-services
dbusinterfacesdir = $(datadir)/dbus-1/interfaces
sysconfexampledir = $(pkgdatadir)

# Pre-defines to allow appending later
bin_PROGRAMS =
libexec_PROGRAMS =
noinst_LTLIBRARIES =
lib_LTLIBRARIES =
systemdsystemunit_DATA =
dist_dbusconf_DATA =
dist_dbussystemservice_DATA =
dist_dbusinterfaces_DATA =
dist_sysconfexample_DATA =
dist_bin_SCRIPTS =
dist_man5_MANS =
dist_man8_MANS =
EXTRA_DIST =
CLEANFILES =
DISTCLEANFILES =
MAINTAINERCLEANFILES =
BUILT_SOURCES =

# Documentation
EXTRA_DIST += README.md

# Ensure systemd units get installed under $(prefix) for distcheck
AM_DISTCHECK_CONFIGURE_FLAGS = --with-systemdsystemunitdir='$${libdir}/systemd/system'

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA =

# Common compilation options
AM_CPPFLAGS = \
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

# Utility library
lib_LTLIBRARIES += libeos-updater-util/libeos-updater-util-@EUU_API_VERSION@.la

enum_header_files = \
	$(top_srcdir)/libeos-updater-util/flatpak.h \
	$(top_srcdir)/libeos-updater-util/types.h \
	$(NULL)

libeos-updater-util/enums.h: libeos-updater-util/enums.h.in $(enum_header_files)
	$(AM_V_GEN)$(GLIB_MKENUMS) --template $< $(enum_header_files) --output $@

libeos-updater-util/enums.c: libeos-updater-util/enums.c.in $(enum_header_files) $(top_builddir)/libeos-updater-util/enums.h
	$(AM_V_GEN)$(GLIB_MKENUMS) --template $< $(enum_header_files) --output $@

BUILT_SOURCES += $(top_builddir)/libeos-updater-util/enums.c $(top_builddir)/libeos-updater-util/enums.h
CLEANFILES += $(top_builddir)/libeos-updater-util/enums.c $(top_builddir)/libeos-updater-util/enums.h
EXTRA_DIST += libeos-updater-util/enums.h.in libeos-updater-util/enums.c.in

libeos_updater_util_libeos_updater_util_@EUU_API_VERSION@_la_CFLAGS = \
	$(AM_CFLAGS) \
	$(STD_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS) \
	$(WARN_CFLAGS) \
	$(FLATPAK_CFLAGS) \
	$(GIO_CFLAGS) \
	$(JSON_GLIB_CFLAGS) \
	$(OSTREE_CFLAGS) \
	$(SOUP_CFLAGS) \
	$(NULL)
libeos_updater_util_libeos_updater_util_@EUU_API_VERSION@_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-DG_LOG_DOMAIN=\""libeos-updater-util"\" \
	$(NULL)
libeos_updater_util_libeos_updater_util_@EUU_API_VERSION@_la_LDFLAGS = \
	$(WARN_LDFLAGS) \
	$(NULL)
libeos_updater_util_libeos_updater_util_@EUU_API_VERSION@_la_LIBADD = \
	$(CODE_COVERAGE_LIBS) \
	$(FLATPAK_LIBS) \
	$(GIO_LIBS) \
	$(JSON_GLIB_LIBS) \
	$(OSTREE_LIBS) \
	$(SOUP_LIBS) \
	$(NULL)
libeos_updater_util_libeos_updater_util_@EUU_API_VERSION@_la_SOURCES = \
	libeos-updater-util/avahi-service-file.c \
	libeos-updater-util/avahi-service-file.h \
	libeos-updater-util/config.c \
	libeos-updater-util/config.h \
	libeos-updater-util/flatpak.c \
	libeos-updater-util/flatpak.h \
	libeos-updater-util/ostree-bloom-private.h \
	libeos-updater-util/ostree-bloom.c \
	libeos-updater-util/ostree.c \
	libeos-updater-util/ostree.h \
	libeos-updater-util/refcounted.h \
	libeos-updater-util/types.c \
	libeos-updater-util/types.h \
	libeos-updater-util/util.c \
	libeos-updater-util/util.h \
	$(NULL)

nodist_libeos_updater_util_libeos_updater_util_@EUU_API_VERSION@_la_SOURCES = \
	libeos-updater-util/enums.c \
	libeos-updater-util/enums.h \
	$(NULL)

# eos-autoupdater program
libexec_PROGRAMS += eos-autoupdater/eos-autoupdater

eos_autoupdater_eos_autoupdater_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-DG_LOG_DOMAIN=\""eos-autoupdater"\" \
	$(NULL)
eos_autoupdater_eos_autoupdater_CFLAGS = \
	$(AM_CFLAGS) \
	$(STD_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS) \
	$(WARN_CFLAGS) \
	$(EOS_AUTOUPDATER_CFLAGS) \
	$(NULL)
eos_autoupdater_eos_autoupdater_LDFLAGS = \
	$(WARN_LDFLAGS) \
	$(NULL)
eos_autoupdater_eos_autoupdater_LDADD = \
	$(CODE_COVERAGE_LIBS) \
	$(top_builddir)/eos-updater/libeos-updater-dbus.la \
	$(top_builddir)/libeos-updater-util/libeos-updater-util-@EUU_API_VERSION@.la \
	$(EOS_AUTOUPDATER_LIBS) \
	$(NULL)
eos_autoupdater_eos_autoupdater_SOURCES = \
	eos-autoupdater/main.c \
	$(NULL)
nodist_eos_autoupdater_eos_autoupdater_SOURCES = \
	$(eos_updater_resource_sources) \
	$(NULL)

eos_autoupdater_in = \
	eos-autoupdater/eos-autoupdater.service.in \
	eos-autoupdater/eos-autoupdater.timer.in \
	$(NULL)

systemdsystemunit_DATA += $(eos_autoupdater_in:%.in=%)
EXTRA_DIST += $(eos_autoupdater_in)
CLEANFILES += $(eos_autoupdater_in:%.in=%)

dist_sysconfexample_DATA += eos-autoupdater/eos-autoupdater.conf

dist_man5_MANS += eos-autoupdater/docs/eos-autoupdater.conf.5
dist_man8_MANS += eos-autoupdater/docs/eos-autoupdater.8

# eos-updater program
noinst_LTLIBRARIES += eos-updater/libeos-updater-dbus.la

eos_updater_codegen_sources = \
	eos-updater/dbus.c \
	eos-updater/dbus.h \
	$(NULL)
BUILT_SOURCES += $(eos_updater_codegen_sources)
CLEANFILES += $(eos_updater_codegen_sources)

eos-updater/dbus.c: eos-updater/dbus.h
eos-updater/dbus.h: eos-updater/com.endlessm.Updater.xml
	$(AM_V_GEN)$(GDBUS_CODEGEN) \
		--interface-prefix com.endlessm. \
		--generate-c-code eos-updater/dbus \
		--c-namespace Eos \
		--c-generate-object-manager \
		--c-generate-autocleanup all \
		$<

eos_updater_libeos_updater_dbus_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-DG_LOG_DOMAIN=\""eos-updater-dbus"\" \
	$(NULL)
eos_updater_libeos_updater_dbus_la_CFLAGS = \
	$(AM_CFLAGS) \
	$(STD_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS) \
	$(WARN_CFLAGS) \
	$(EOS_UPDATER_CFLAGS) \
	$(NULL)
eos_updater_libeos_updater_dbus_la_LDFLAGS = \
	$(WARN_LDFLAGS) \
	$(NULL)
eos_updater_libeos_updater_dbus_la_LIBADD = \
	$(CODE_COVERAGE_LIBS) \
	$(EOS_UPDATER_LIBS) \
	$(NULL)
eos_updater_libeos_updater_dbus_la_SOURCES = \
	eos-updater/object.c \
	eos-updater/object.h \
	$(NULL)
nodist_eos_updater_libeos_updater_dbus_la_SOURCES = $(eos_updater_codegen_sources)

bin_PROGRAMS += eos-updater/eos-updater

eos_updater_eos_updater_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-DG_LOG_DOMAIN=\""eos-updater"\" \
	$(NULL)
eos_updater_eos_updater_CFLAGS = \
	$(AM_CFLAGS) \
	$(STD_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS) \
	$(WARN_CFLAGS) \
	$(EOS_UPDATER_CFLAGS) \
	$(NULL)
eos_updater_eos_updater_LDFLAGS = \
	$(WARN_LDFLAGS) \
	$(NULL)
eos_updater_eos_updater_LDADD  = \
	$(CODE_COVERAGE_LIBS) \
	$(top_builddir)/eos-updater/libeos-updater-dbus.la \
	$(top_builddir)/libeos-updater-util/libeos-updater-util-@EUU_API_VERSION@.la \
	$(EOS_UPDATER_LIBS) \
	$(NULL)
eos_updater_eos_updater_SOURCES = \
	eos-updater/apply.c \
	eos-updater/apply.h \
	eos-updater/data.h \
	eos-updater/data.c \
	eos-updater/fetch.c \
	eos-updater/fetch.h \
	eos-updater/live-boot.c \
	eos-updater/live-boot.h \
	eos-updater/poll.c \
	eos-updater/poll.h \
	eos-updater/poll-common.h \
	eos-updater/poll-common.c \
	eos-updater/main.c \
	$(NULL)
nodist_eos_updater_eos_updater_SOURCES = \
	$(eos_updater_resource_sources) \
	$(NULL)

eos_updater_resource_sources = \
	eos-updater/resources.c \
	eos-updater/resources.h \
	$(NULL)
eos_updater_resource_deps = $(shell glib-compile-resources --sourcedir=$(top_srcdir)/eos-updater --generate-dependencies $(top_srcdir)/eos-updater/eos-updater.gresource.xml)
eos-updater/resources.c: eos-updater/eos-updater.gresource.xml $(eos_updater_resource_deps)
	$(AM_V_GEN) $(GLIB_COMPILE_RESOURCES) \
		--c-name=eos_updater_resources --generate-source --internal \
		--sourcedir=$(top_srcdir)/eos-updater --target=$@ $<
eos-updater/resources.h: eos-updater/eos-updater.gresource.xml $(eos_updater_resource_deps)
	$(AM_V_GEN) $(GLIB_COMPILE_RESOURCES) \
		--c-name=eos_updater_resources --generate-header --internal \
		--sourcedir=$(top_srcdir)/eos-updater --target=$@ $<

BUILT_SOURCES += $(eos_updater_resource_sources)
CLEANFILES += $(eos_updater_resource_sources)
EXTRA_DIST += \
	eos-updater/eos-updater.gresource.xml \
	$(eos_updater_resource_deps) \
	$(NULL)

dist_dbusconf_DATA += eos-updater/com.endlessm.Updater.conf
dist_dbussystemservice_DATA += eos-updater/com.endlessm.Updater.service
dist_dbusinterfaces_DATA += eos-updater/com.endlessm.Updater.xml

eos_updater_in = \
	eos-updater/eos-updater.service.in \
	$(NULL)

systemdsystemunit_DATA += $(eos_updater_in:%.in=%)
EXTRA_DIST += $(eos_updater_in)
CLEANFILES += $(eos_updater_in:%.in=%)

dist_sysconfexample_DATA += eos-updater/eos-updater.conf

dist_man5_MANS += eos-updater/docs/eos-updater.conf.5
dist_man8_MANS += eos-updater/docs/eos-updater.8

# eos-updater-avahi program
libexec_PROGRAMS += eos-updater-avahi/eos-updater-avahi

eos_updater_avahi_eos_updater_avahi_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-DG_LOG_DOMAIN=\""eos-updater-avahi"\" \
	$(NULL)
eos_updater_avahi_eos_updater_avahi_CFLAGS = \
	$(AM_CFLAGS) \
	$(STD_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS) \
	$(WARN_CFLAGS) \
	$(EOS_UPDATER_AVAHI_CFLAGS) \
	$(NULL)
eos_updater_avahi_eos_updater_avahi_LDFLAGS = \
	$(WARN_LDFLAGS) \
	$(NULL)
eos_updater_avahi_eos_updater_avahi_LDADD = \
	$(CODE_COVERAGE_LIBS) \
	$(top_builddir)/libeos-update-server/libeos-update-server-@EUS_API_VERSION@.la \
	$(top_builddir)/libeos-updater-util/libeos-updater-util-@EUU_API_VERSION@.la \
	$(EOS_UPDATER_AVAHI_LIBS) \
	$(NULL)
eos_updater_avahi_eos_updater_avahi_SOURCES = \
	eos-updater-avahi/eos-updater-avahi.c \
	$(NULL)

eos_updater_avahi_in = \
	eos-updater-avahi/eos-updater-avahi.path.in \
	eos-updater-avahi/eos-updater-avahi.service.in \
	$(NULL)

systemdsystemunit_DATA += $(eos_updater_avahi_in:%.in=%)
EXTRA_DIST += $(eos_updater_avahi_in)
CLEANFILES += $(eos_updater_avahi_in:%.in=%)

dist_man8_MANS += eos-updater-avahi/docs/eos-updater-avahi.8

# eos-updater-ctl program
dist_bin_SCRIPTS += eos-updater-ctl/eos-updater-ctl
dist_man8_MANS += eos-updater-ctl/docs/eos-updater-ctl.8

check-local: eos-updater-ctl/eos-updater-ctl.flake8

# Server utility library
noinst_LTLIBRARIES += libeos-update-server/libeos-update-server-@EUS_API_VERSION@.la

libeos_update_server_libeos_update_server_@EUS_API_VERSION@_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-DG_LOG_DOMAIN=\""libeos-update-server"\" \
	$(NULL)
libeos_update_server_libeos_update_server_@EUS_API_VERSION@_la_CFLAGS = \
	$(AM_CFLAGS) \
	$(STD_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS) \
	$(WARN_CFLAGS) \
	$(EOS_UPDATE_SERVER_CFLAGS) \
	$(NULL)
libeos_update_server_libeos_update_server_@EUS_API_VERSION@_la_LDFLAGS = \
	$(WARN_LDFLAGS) \
	$(NULL)
libeos_update_server_libeos_update_server_@EUS_API_VERSION@_la_LIBADD = \
	$(CODE_COVERAGE_LIBS) \
	$(top_builddir)/libeos-updater-util/libeos-updater-util-@EUU_API_VERSION@.la \
	$(EOS_UPDATE_SERVER_LIBS) \
	$(NULL)
libeos_update_server_libeos_update_server_@EUS_API_VERSION@_la_SOURCES = \
	libeos-update-server/config.c \
	libeos-update-server/config.h \
	libeos-update-server/repo.c \
	libeos-update-server/repo.h \
	libeos-update-server/server.c \
	libeos-update-server/server.h \
	$(NULL)
nodist_libeos_update_server_libeos_update_server_@EUS_API_VERSION@_la_SOURCES = \
	$(libeos_update_server_resource_sources) \
	$(NULL)

libeos_update_server_resource_sources = \
	libeos-update-server/resources.c \
	libeos-update-server/resources.h \
	$(NULL)
libeos_update_server_resource_deps = $(shell glib-compile-resources --sourcedir=$(top_srcdir)/libeos-update-server --generate-dependencies $(top_srcdir)/libeos-update-server/libeos-update-server.gresource.xml)
libeos-update-server/resources.c: libeos-update-server/libeos-update-server.gresource.xml $(libeos_update_server_resource_deps)
	$(AM_V_GEN) $(GLIB_COMPILE_RESOURCES) \
		--c-name=eus_resources --generate-source --internal \
		--sourcedir=$(top_srcdir)/libeos-update-server --target=$@ $<
libeos-update-server/resources.h: libeos-update-server/libeos-update-server.gresource.xml $(libeos_update_server_resource_deps)
	$(AM_V_GEN) $(GLIB_COMPILE_RESOURCES) \
		--c-name=eus_resources --generate-header --internal \
		--sourcedir=$(top_srcdir)/libeos-update-server --target=$@ $<

BUILT_SOURCES += $(libeos_update_server_resource_sources)
CLEANFILES += $(libeos_update_server_resource_sources)
EXTRA_DIST += \
	libeos-update-server/libeos-update-server.gresource.xml \
	$(libeos_update_server_resource_deps) \
	$(NULL)

# eos-update-server program
libexec_PROGRAMS += eos-update-server/eos-update-server

eos_update_server_eos_update_server_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-DG_LOG_DOMAIN=\""eos-update-server"\" \
	$(NULL)
eos_update_server_eos_update_server_CFLAGS = \
	$(AM_CFLAGS) \
	$(STD_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS) \
	$(WARN_CFLAGS) \
	$(EOS_UPDATE_SERVER_CFLAGS) \
	$(NULL)
eos_update_server_eos_update_server_LDFLAGS = \
	$(WARN_LDFLAGS) \
	$(NULL)
eos_update_server_eos_update_server_LDADD = \
	$(CODE_COVERAGE_LIBS) \
	$(top_builddir)/libeos-update-server/libeos-update-server-@EUS_API_VERSION@.la \
	$(top_builddir)/libeos-updater-util/libeos-updater-util-@EUU_API_VERSION@.la \
	$(EOS_UPDATE_SERVER_LIBS) \
	$(NULL)
eos_update_server_eos_update_server_SOURCES = \
	eos-update-server/eos-update-server.c \
	$(NULL)

eos_update_server_in = \
	eos-update-server/eos-update-server.socket.in \
	eos-update-server/eos-update-server.service.in \
	$(NULL)

systemdsystemunit_DATA += $(eos_update_server_in:%.in=%)
EXTRA_DIST += $(eos_update_server_in)
CLEANFILES += $(eos_update_server_in:%.in=%)

dist_sysconfexample_DATA += eos-update-server/eos-update-server.conf

dist_man5_MANS += eos-update-server/docs/eos-update-server.conf.5
dist_man8_MANS += eos-update-server/docs/eos-update-server.8

# eos-updater-prepare-volume utility
dist_bin_SCRIPTS += eos-updater-prepare-volume/eos-updater-prepare-volume

dist_man8_MANS += eos-updater-prepare-volume/docs/eos-updater-prepare-volume.8

# Flatpak installer utility library
lib_LTLIBRARIES += libeos-updater-flatpak-installer/libeos-updater-flatpak-installer-@EUFI_API_VERSION@.la

libeos_updater_flatpak_installer_libeos_updater_flatpak_installer_@EUFI_API_VERSION@_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-DG_LOG_DOMAIN=\""libeos-updater-flatpak-installer"\" \
	$(NULL)
libeos_updater_flatpak_installer_libeos_updater_flatpak_installer_@EUFI_API_VERSION@_la_CFLAGS = \
	$(AM_CFLAGS) \
	$(STD_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS) \
	$(WARN_CFLAGS) \
	$(EOS_UPDATER_FLATPAK_INSTALLER_CFLAGS) \
	$(NULL)
libeos_updater_flatpak_installer_libeos_updater_flatpak_installer_@EUFI_API_VERSION@_la_LDFLAGS = \
	$(WARN_LDFLAGS) \
	$(NULL)
libeos_updater_flatpak_installer_libeos_updater_flatpak_installer_@EUFI_API_VERSION@_la_LIBADD = \
	$(CODE_COVERAGE_LIBS) \
	$(EOS_UPDATER_FLATPAK_INSTALLER_LIBS) \
	$(top_builddir)/libeos-updater-util/libeos-updater-util-@EUU_API_VERSION@.la \
	$(NULL)
libeos_updater_flatpak_installer_libeos_updater_flatpak_installer_@EUFI_API_VERSION@_la_SOURCES = \
	libeos-updater-flatpak-installer/determine-flatpak-actions.c \
	libeos-updater-flatpak-installer/installer.h \
	libeos-updater-flatpak-installer/perform-flatpak-actions.c \
	$(NULL)


# eos-updater-flatpak-installer program
libexec_PROGRAMS += eos-updater-flatpak-installer/eos-updater-flatpak-installer

# GObject Introspection
-include $(INTROSPECTION_MAKEFILE)
INTROSPECTION_GIRS =
INTROSPECTION_SCANNER_ARGS =
INTROSPECTION_COMPILER_ARGS =
AM_DISTCHECK_CONFIGURE_FLAGS += --enable-introspection
EXTRA_DIST += m4/introspection.m4

if HAVE_INTROSPECTION
# GObject Introspection for libeos-updater-util
libeos-updater-util/EosUpdaterUtil-@EUU_API_VERSION@.gir: libeos-updater-util/libeos-updater-util-@EUU_API_VERSION@.la
libeos_updater_util_EosUpdaterUtil_@EUU_API_VERSION@_gir_INCLUDES = GObject-2.0 Gio-2.0 Flatpak-1.0 OSTree-1.0
libeos_updater_util_EosUpdaterUtil_@EUU_API_VERSION@_gir_CFLAGS = \
	$(libeos_updater_util_libeos_updater_util_@EUU_API_VERSION@_la_CPPFLAGS) \
	$(libeos_updater_util_libeos_updater_util_@EUU_API_VERSION@_la_CFLAGS)
libeos_updater_util_EosUpdaterUtil_@EUU_API_VERSION@_gir_FILES = \
	$(filter-out %-private.h,$(libeos_updater_util_libeos_updater_util_@EUU_API_VERSION@_la_SOURCES))
libeos_updater_util_EosUpdaterUtil_@EUU_API_VERSION@_gir_LIBS = libeos-updater-util/libeos-updater-util-@EUU_API_VERSION@.la
libeos_updater_util_EosUpdaterUtil_@EUU_API_VERSION@_gir_NAMESPACE = EosUpdaterUtil
libeos_updater_util_EosUpdaterUtil_@EUU_API_VERSION@_gir_SCANNERFLAGS = \
	$(WARN_SCANNERFLAGS) \
	--nsversion=@EUU_API_VERSION@ \
	--c-include="libeos-updater-util/util.h" \
	--identifier-prefix=EosUpdater \
	--identifier-prefix=EosOstreeAvahi \
	--identifier-prefix=EosAvahi \
	--identifier-prefix=Euu \
	--symbol-prefix=euu \
	--symbol-prefix=eos_updater \
	--symbol-prefix=eos_ostree_avahi \
	--symbol-prefix=eos_avahi \
	$(NULL)

INTROSPECTION_GIRS += libeos-updater-util/EosUpdaterUtil-@EUU_API_VERSION@.gir

# GObject Introspection for libeos-updater-flatpak-installer
libeos-updater-flatpak-installer/EosUpdaterFlatpakInstaller-@EUFI_API_VERSION@.gir: libeos-updater-flatpak-installer/libeos-updater-flatpak-installer-@EUFI_API_VERSION@.la libeos-updater-util/EosUpdaterUtil-@EUU_API_VERSION@.gir
libeos_updater_flatpak_installer_EosUpdaterFlatpakInstaller_@EUFI_API_VERSION@_gir_INCLUDES = GObject-2.0 Gio-2.0 Flatpak-1.0 EosUpdaterUtil-0
libeos_updater_flatpak_installer_EosUpdaterFlatpakInstaller_@EUFI_API_VERSION@_gir_CFLAGS = \
	$(STD_CFLAGS) \
	$(EOS_UPDATER_FLATPAK_INSTALLER_CFLAGS) \
	$(NULL)
libeos_updater_flatpak_installer_EosUpdaterFlatpakInstaller_@EUFI_API_VERSION@_gir_FILES = \
	$(libeos_updater_flatpak_installer_libeos_updater_flatpak_installer_@EUFI_API_VERSION@_la_SOURCES)
libeos_updater_flatpak_installer_EosUpdaterFlatpakInstaller_@EUFI_API_VERSION@_gir_LIBS = libeos-updater-flatpak-installer/libeos-updater-flatpak-installer-@EUFI_API_VERSION@.la
libeos_updater_flatpak_installer_EosUpdaterFlatpakInstaller_@EUFI_API_VERSION@_gir_NAMESPACE = EosUpdaterFlatpakInstaller
libeos_updater_flatpak_installer_EosUpdaterFlatpakInstaller_@EUFI_API_VERSION@_gir_SCANNERFLAGS = \
	$(WARN_SCANNERFLAGS) \
	--add-include-path=$(top_builddir)/libeos-updater-util \
	--nsversion=@EUFI_API_VERSION@ \
	--c-include="libeos-updater-flatpak-installer/installer.h" \
	--identifier-prefix=Eufi \
	--symbol-prefix=eufi \
	-I$(top_srcdir) \
	$(NULL)

INTROSPECTION_COMPILER_ARGS += --includedir=$(top_builddir)/libeos-updater-util
INTROSPECTION_GIRS += libeos-updater-flatpak-installer/EosUpdaterFlatpakInstaller-@EUFI_API_VERSION@.gir

girdir = $(datadir)/gir-1.0
gir_DATA = $(INTROSPECTION_GIRS)

typelibdir = $(libdir)/girepository-1.0
typelib_DATA = $(INTROSPECTION_GIRS:.gir=.typelib)

CLEANFILES += $(gir_DATA) $(typelib_DATA)
endif # HAVE_INTROSPECTION

eos_updater_flatpak_installer_eos_updater_flatpak_installer_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-DG_LOG_DOMAIN=\""eos-updater-flatpak-installer"\" \
	$(NULL)
eos_updater_flatpak_installer_eos_updater_flatpak_installer_CFLAGS = \
	$(AM_CFLAGS) \
	$(STD_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS) \
	$(WARN_CFLAGS) \
	$(EOS_UPDATER_FLATPAK_INSTALLER_CFLAGS) \
	$(NULL)
eos_updater_flatpak_installer_eos_updater_flatpak_installer_LDFLAGS = \
	$(WARN_LDFLAGS) \
	$(NULL)
eos_updater_flatpak_installer_eos_updater_flatpak_installer_LDADD = \
	$(CODE_COVERAGE_LIBS) \
	$(EOS_UPDATER_FLATPAK_INSTALLER_LIBS) \
	$(top_builddir)/libeos-updater-util/libeos-updater-util-@EUU_API_VERSION@.la \
	$(top_builddir)/libeos-updater-flatpak-installer/libeos-updater-flatpak-installer-@EUFI_API_VERSION@.la \
	$(NULL)
eos_updater_flatpak_installer_eos_updater_flatpak_installer_SOURCES = \
	eos-updater-flatpak-installer/main.c \
	$(NULL)

eos_updater_flatpak_installer_in = \
	eos-updater-flatpak-installer/eos-updater-flatpak-installer.service.in \
	$(NULL)

systemdsystemunit_DATA += $(eos_updater_flatpak_installer_in:%.in=%)

eos_updater_flatpak_installer_schemas = eos-updater-flatpak-installer/eos-updater-autoinstall.schema.json
flatpakinstallerschemadir = $(datadir)/eos-updater/schemas
flatpakinstallerschema_DATA = $(eos_updater_flatpak_installer_schemas)

EXTRA_DIST += \
	$(eos_updater_flatpak_installer_in) \
	$(eos_updater_flatpak_installer_schemas) \
	$(NULL)

CLEANFILES += $(eos_updater_flatpak_installer_in:%.in=%)

dist_man5_MANS += eos-updater-flatpak-installer/docs/eos-updater-flatpak-autoinstall.d.5
dist_man8_MANS += eos-updater-flatpak-installer/docs/eos-updater-flatpak-installer.8

# Test helper library
noinst_LTLIBRARIES += test-common/libeos-updater-test-common.la

test_common_libeos_updater_test_common_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-DOSTREE_BINARY=\""$(OSTREE)"\" \
	-DGPG_BINARY=\""$(GPG)"\" \
	-DFLATPAK_BINARY=\""$(FLATPAK)"\" \
	-DOSTREE_WITH_AUTOCLEANUPS \
	-DG_LOG_DOMAIN=\""eos-updater-test-common"\" \
	$(NULL)
test_common_libeos_updater_test_common_la_CFLAGS = \
	$(AM_CFLAGS) \
	$(STD_CFLAGS) \
	$(WARN_CFLAGS) \
	$(GIO_CFLAGS) \
	$(OSTREE_CFLAGS) \
	$(NULL)
test_common_libeos_updater_test_common_la_LDFLAGS = \
	$(WARN_LDFLAGS) \
	$(NULL)
test_common_libeos_updater_test_common_la_LIBADD = \
	$(GIO_LIBS) \
	$(OSTREE_LIBS) \
	$(top_builddir)/libeos-updater-util/libeos-updater-util-@EUU_API_VERSION@.la \
	$(NULL)
test_common_libeos_updater_test_common_la_SOURCES = \
	test-common/convenience.c \
	test-common/convenience.h \
	test-common/flatpak-spawn.c \
	test-common/flatpak-spawn.h \
	test-common/gpg.c \
	test-common/gpg.h \
	test-common/misc-utils.c \
	test-common/misc-utils.h \
	test-common/ostree-spawn.c \
	test-common/ostree-spawn.h \
	test-common/spawn-utils.c \
	test-common/spawn-utils.h \
	test-common/utils.c \
	test-common/utils.h \
	$(NULL)

tests_services_in = \
	tests/services/org.freedesktop.Flatpak.SystemHelper.service.in \
	$(NULL)

systemdsystemunit_DATA += $(tests_services_in:%.in=%)
EXTRA_DIST += $(tests_services_in)
CLEANFILES += $(tests_services_in:%.in=%)

# Generic systemd rules
edit = sed \
	-e 's|[@]bindir[@]|$(bindir)|g' \
	-e 's|[@]libexecdir[@]|$(libexecdir)|g' \
	-e 's|[@]localstatedir[@]|$(localstatedir)|g' \
	-e 's|[@]pkgdatadir[@]|$(pkgdatadir)|g' \
	-e 's|[@]prefix[@]|$(prefix)|g' \
	-e 's|[@]sbindir[@]|$(sbindir)|g' \
	-e 's|[@]server_port[@]|$(server_port)|g' \
	-e 's|[@]sysconfdir[@]|$(sysconfdir)|g' \
	$(NULL)

%.service: %.service.in Makefile
	mkdir -p `dirname $@`
	$(AM_V_GEN)$(edit) $< > $@.tmp && mv $@.tmp $@
%.socket: %.socket.in Makefile
	$(AM_V_GEN)$(edit) $< > $@.tmp && mv $@.tmp $@
%.timer: %.timer.in Makefile
	$(AM_V_GEN)$(edit) $< > $@.tmp && mv $@.tmp $@
%.path: %.path.in Makefile
	$(AM_V_GEN)$(edit) $< > $@.tmp && mv $@.tmp $@

# Generic Python checking rules
%.flake8: %
	$(AM_V_GEN) python3 -m flake8 $<

# Cleaning
EXTRA_DIST += \
	autogen.sh \
	glib-tap.mk \
	tap-test \
	$(NULL)

MAINTAINERCLEANFILES += \
	$(GITIGNORE_MAINTAINERCLEANFILES_TOPLEVEL) \
	$(GITIGNORE_MAINTAINERCLEANFILES_MAKEFILE_IN) \
	$(GITIGNORE_MAINTAINERCLEANFILES_M4_LIBTOOL) \
	build-aux/config.rpath \
	build-aux/tap-driver.sh \
	gtk-doc.make \
	m4/ax_append_compile_flags.m4 \
	m4/ax_append_flag.m4 \
	m4/ax_append_link_flags.m4 \
	m4/ax_check_compile_flag.m4 \
	m4/ax_check_link_flag.m4 \
	m4/ax_code_coverage.m4 \
	m4/ax_compiler_flags.m4 \
	m4/ax_compiler_flags_cflags.m4 \
	m4/ax_compiler_flags_gir.m4 \
	m4/ax_compiler_flags_ldflags.m4 \
	m4/ax_is_release.m4 \
	m4/ax_pkg_check_modules.m4 \
	m4/ax_require_defined.m4 \
	m4/ax_valgrind_check.m4 \
	m4/gtk-doc.m4 \
	m4/pkg.m4 \
	$(NULL)

-include $(top_srcdir)/git.mk
