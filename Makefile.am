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

SUBDIRS = . src libeos-updater-util/tests libeos-updater-flatpak-installer/tests data tests

sysconfexampledir = $(pkgdatadir)

# Pre-defines to allow appending later
libexec_PROGRAMS =
noinst_LTLIBRARIES =
systemdsystemunit_DATA =
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
AM_DISTCHECK_CONFIGURE_FLAGS = --with-systemdsystemunitdir='$${libdir}/systemd/system' --enable-introspection

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
noinst_LTLIBRARIES += libeos-updater-util/libeos-updater-util-@EUU_API_VERSION@.la

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
	libeos-updater-util/metrics-private.h \
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
noinst_LTLIBRARIES += libeos-updater-flatpak-installer/libeos-updater-flatpak-installer-@EUFI_API_VERSION@.la

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
	$(top_builddir)/libeos-updater-util/libeos-updater-util-@EUFI_API_VERSION@.la \
	$(NULL)
libeos_updater_flatpak_installer_libeos_updater_flatpak_installer_@EUFI_API_VERSION@_la_SOURCES = \
	libeos-updater-flatpak-installer/determine-flatpak-actions.c \
	libeos-updater-flatpak-installer/installer.h \
	libeos-updater-flatpak-installer/perform-flatpak-actions.c \
	$(NULL)


# eos-updater-flatpak-installer program
libexec_PROGRAMS += eos-updater-flatpak-installer/eos-updater-flatpak-installer

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
	$(top_builddir)/libeos-updater-flatpak-installer/libeos-updater-flatpak-installer-@EUFI_API_VERSION@.la
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
	$(AM_V_GEN)$(edit) $< > $@.tmp && mv $@.tmp $@
%.socket: %.socket.in Makefile
	$(AM_V_GEN)$(edit) $< > $@.tmp && mv $@.tmp $@
%.path: %.path.in Makefile
	$(AM_V_GEN)$(edit) $< > $@.tmp && mv $@.tmp $@

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
