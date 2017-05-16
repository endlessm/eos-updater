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

installed_testdir = $(libexecdir)/installed-tests/libeos-updater-util-@EUU_API_VERSION@
installed_test_metadir = $(datadir)/installed-tests/libeos-updater-util-@EUU_API_VERSION@

# Flags for all test binaries
AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_builddir) \
	-include "config.h" \
	-DG_LOG_DOMAIN=\"libeos-updater-util-tests\" \
	$(NULL)
AM_CFLAGS = \
	$(WARN_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS) \
	$(EOS_UPDATER_UTIL_TESTS_CFLAGS) \
	$(NULL)
AM_LDFLAGS = \
	$(WARN_LDFLAGS) \
	$(CODE_COVERAGE_LDFLAGS) \
	$(NULL)
LDADD = \
	$(top_builddir)/libeos-updater-util/libeos-updater-util-@EUU_API_VERSION@.la \
	$(CODE_COVERAGE_LIBS) \
	$(EOS_UPDATER_UTIL_TESTS_LIBS) \
	$(NULL)

@VALGRIND_CHECK_RULES@
@CODE_COVERAGE_RULES@
CODE_COVERAGE_DIRECTORY = $(top_builddir)/libeos-updater-util
CODE_COVERAGE_IGNORE_PATTERN = \
	"*-autocleanups.h" \
	gmem.h \
	gobject.h \
	gtypes.h \
	$(NULL)

test_programs = \
	avahi-service-file \
	config \
	ostree \
	util \
	$(NULL)

avahi_service_file_SOURCES = avahi-service-file.c
config_SOURCES = config.c
ostree_SOURCES = ostree.c
util_SOURCES = util.c

-include $(top_srcdir)/git.mk
