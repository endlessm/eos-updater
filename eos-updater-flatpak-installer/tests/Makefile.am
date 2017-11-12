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

include $(top_srcdir)/glib-tap.mk

installed_testdir = $(libexecdir)/installed-tests/libeos-updater-flatpak-installer-@EUU_API_VERSION@
installed_test_metadir = $(datadir)/installed-tests/libeos-updater-flatpak-installer-@EUU_API_VERSION@

# Flags for all test binaries
AM_CPPFLAGS = \
	-I$(abs_top_srcdir) \
	-I$(abs_top_builddir) \
	-include "config.h" \
	-DG_LOG_DOMAIN=\"libeos-updater-flatpak-installer-tests\" \
	$(NULL)
AM_CFLAGS = \
	$(STD_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS) \
	$(WARN_CFLAGS) \
	$(EOS_UPDATER_FLATPAK_INSTALLER_TESTS_CFLAGS) \
	$(NULL)
AM_LDFLAGS = \
	$(WARN_LDFLAGS) \
	$(CODE_COVERAGE_LDFLAGS) \
	$(NULL)
LDADD = \
	$(top_builddir)/eos-updater-flatpak-installer/libeos-updater-flatpak-installer-@EUU_API_VERSION@.la \
	$(top_builddir)/libeos-updater-util/libeos-updater-util-@EUU_API_VERSION@.la \
	$(top_builddir)/test-common/libeos-updater-test-common.la \
	$(CODE_COVERAGE_LIBS) \
	$(EOS_UPDATER_UTIL_TESTS_LIBS) \
	$(NULL)

@VALGRIND_CHECK_RULES@
@CODE_COVERAGE_RULES@
CODE_COVERAGE_DIRECTORY = $(top_builddir)/eos-updater-flatpak-installer
CODE_COVERAGE_IGNORE_PATTERN = \
	"*-autocleanups.h" \
	gmem.h \
	gobject.h \
	gtypes.h \
	$(NULL)

test_programs = \
	installer \
	$(NULL)

installer_SOURCES = installer.c

-include $(top_srcdir)/git.mk
