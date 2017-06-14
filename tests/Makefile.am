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

installed_testdir = $(libexecdir)/installed-tests/eos-updater-@EU_API_VERSION@
installed_test_metadir = $(datadir)/installed-tests/eos-updater-@EU_API_VERSION@

# Test utility library
testlib = libeos-updater-test.la
noinst_LTLIBRARIES += $(testlib)

libeos_updater_test_la_CPPFLAGS = \
	-DOSTREE_BINARY=\""$(OSTREE)"\" \
	-DGPG_BINARY=\""$(GPG)"\" \
	-DOSTREE_WITH_AUTOCLEANUPS \
	-I$(top_srcdir) \
	-I$(top_builddir) \
	-include "config.h" \
	-I$(top_srcdir)/src \
	-I$(top_builddir)/src \
	$(NULL)
libeos_updater_test_la_CFLAGS = \
	$(WARN_CFLAGS) \
	$(OSTREE_CFLAGS) \
	$(GIO_CFLAGS) \
	$(NULL)
libeos_updater_test_la_LDFLAGS = $(WARN_LDFLAGS)
libeos_updater_test_la_LIBADD = $(OSTREE_LIBS) $(GIO_LIBS)
libeos_updater_test_la_SOURCES = \
	spawn-utils.c \
	spawn-utils.h \
	misc-utils.c \
	misc-utils.h \
	ostree-spawn.c \
	ostree-spawn.h \
	eos-test-utils.c \
	eos-test-utils.h \
	eos-test-convenience.c \
	eos-test-convenience.h \
	$(NULL)

@VALGRIND_CHECK_RULES@
@CODE_COVERAGE_RULES@
CODE_COVERAGE_IGNORE_PATTERN = \
	eos-updater-generated.c \
	"*-autocleanups.h" \
	gmem.h \
	gobject.h \
	gtypes.h \
	$(NULL)

# FIXME: Ensure these tests can be run installed as well.
uninstalled_test_programs = \
	test-update-from-main \
	test-update-from-lan \
	test-update-from-volume \
	$(NULL)

AM_TESTS_ENVIRONMENT = \
	export \
	OSTREE_SYSROOT_DEBUG=mutable-deployments \
	$(if $(EOS_UPDATER_GDB),EOS_CHECK_UPDATER_GDB_BASH_PATH=$(EOS_UPDATER_GDB)) \
	$(if $(EOS_UPDATE_SERVER_GDB_BASE),EOS_CHECK_UPDATE_SERVER_GDB_BASH_PATH_BASE=$(EOS_UPDATE_SERVER_GDB_BASE)) \
	$(if $(EOS_UPDATER_PREPARE_VOLUME_GDB),EOS_CHECK_UPDATER_PREPARE_VOLUME_GDB_BASH_PATH=$(EOS_UPDATER_PREPARE_VOLUME_GDB)) \
	;

test_cppflags = \
	-I$(top_srcdir) \
	-I$(top_builddir) \
	-include "config.h" \
	-I$(top_srcdir)/src \
	-I$(top_builddir)/src \
	-DG_LOG_DOMAIN=\"eos-updater-tests\" \
	$(NULL)
test_cflags = \
	$(WARN_CFLAGS) \
	$(GIO_CFLAGS) \
	$(NULL)
test_ldflags = \
	$(WARN_LDFLAGS) \
	$(NULL)
test_ldadd = \
	$(GIO_LIBS) \
	$(testlib) \
	$(NULL)

test_update_from_main_CPPFLAGS = $(test_cppflags)
test_update_from_main_CFLAGS = $(test_cflags)
test_update_from_main_LDFLAGS = $(test_ldflags)
test_update_from_main_LDADD = $(test_ldadd)
test_update_from_main_SOURCES = test-update-from-main.c

test_update_from_lan_CPPFLAGS = $(test_cppflags)
test_update_from_lan_CFLAGS = $(test_cflags)
test_update_from_lan_LDFLAGS = $(test_ldflags)
test_update_from_lan_LDADD = $(test_ldadd)
test_update_from_lan_SOURCES = test-update-from-lan.c

test_update_from_volume_CPPFLAGS = $(test_cppflags)
test_update_from_volume_CFLAGS = $(test_cflags)
test_update_from_volume_LDFLAGS = $(test_ldflags)
test_update_from_volume_LDADD = $(test_ldadd)
test_update_from_volume_SOURCES = test-update-from-volume.c

dist_uninstalled_test_data = \
	gpghome/C1EB8F4E.asc \
	gpghome/keyid \
	gpghome/pubring.gpg \
	gpghome/random_seed \
	gpghome/secring.gpg \
	$(NULL)

# Installed integration tests
dist_installed_test_scripts = \
	eos-updater-avahi.py \
	eos-update-server.py \
	$(NULL)
dist_test_extra_scripts = \
	taptestrunner.py \
	$(NULL)

# Our Python integration tests don’t need the --tap argument
%.py.test: %.py Makefile
	$(AM_V_GEN) (echo '[Test]' > $@.tmp; \
	echo 'Type=session' >> $@.tmp; \
	echo 'Exec=$(installed_testdir)/$(notdir $<)' >> $@.tmp; \
	echo 'Output=TAP' >> $@.tmp; \
	mv $@.tmp $@)

-include $(top_srcdir)/git.mk
