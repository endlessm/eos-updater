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

@VALGRIND_CHECK_RULES@
@CODE_COVERAGE_RULES@
CODE_COVERAGE_IGNORE_PATTERN = \
	eos-updater/dbus.c \
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
	test-update-cleanup-workaround \
	test-update-broken-delta \
	test-update-install-flatpaks \
	test-update-missing-deployed-commit \
	test-update-refspec-checkpoint \
	test-update-refspec-endoflife \
	test-update-direct \
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
	-DG_LOG_DOMAIN=\"eos-updater-tests\" \
	$(NULL)
test_cflags = \
	$(AM_CFLAGS) \
	$(STD_CFLAGS) \
	$(WARN_CFLAGS) \
	$(OSTREE_CFLAGS) \
	$(GIO_CFLAGS) \
	$(NULL)
test_ldflags = \
	$(WARN_LDFLAGS) \
	$(NULL)
test_ldadd = \
	$(OSTREE_LIBS) \
	$(GIO_LIBS) \
	$(top_builddir)/test-common/libeos-updater-test-common.la \
	$(top_builddir)/libeos-updater-util/libeos-updater-util-@EUU_API_VERSION@.la \
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

test_update_cleanup_workaround_CPPFLAGS = $(test_cppflags)
test_update_cleanup_workaround_CFLAGS = $(test_cflags)
test_update_cleanup_workaround_LDFLAGS = $(test_ldflags)
test_update_cleanup_workaround_LDADD = $(test_ldadd)
test_update_cleanup_workaround_SOURCES = test-update-cleanup-workaround.c

test_update_install_flatpaks_CPPFLAGS = $(test_cppflags)
test_update_install_flatpaks_CFLAGS = $(test_cflags) $(FLATPAK_CFLAGS) $(JSON_GLIB_CFLAGS)
test_update_install_flatpaks_LDFLAGS = $(test_ldflags)
test_update_install_flatpaks_LDADD = $(test_ldadd) $(FLATPAK_LIBS) $(JSON_GLIB_LIBS)
test_update_install_flatpaks_SOURCES = test-update-install-flatpaks.c

test_update_broken_delta_CPPFLAGS = $(test_cppflags)
test_update_broken_delta_CFLAGS = $(test_cflags)
test_update_broken_delta_LDFLAGS = $(test_ldflags)
test_update_broken_delta_LDADD = $(test_ldadd)
test_update_broken_delta_SOURCES = test-update-broken-delta.c

test_update_missing_deployed_commit_CPPFLAGS = $(test_cppflags)
test_update_missing_deployed_commit_CFLAGS = $(test_cflags)
test_update_missing_deployed_commit_LDFLAGS = $(test_ldflags)
test_update_missing_deployed_commit_LDADD = $(test_ldadd)
test_update_missing_deployed_commit_SOURCES = test-update-missing-deployed-commit.c

test_update_refspec_checkpoint_CPPFLAGS = $(test_cppflags)
test_update_refspec_checkpoint_CFLAGS = $(test_cflags)
test_update_refspec_checkpoint_LDFLAGS = $(test_ldflags)
test_update_refspec_checkpoint_LDADD = $(test_ldadd)
test_update_refspec_checkpoint_SOURCES = test-update-refspec-checkpoint.c

test_update_refspec_endoflife_CPPFLAGS = $(test_cppflags)
test_update_refspec_endoflife_CFLAGS = $(test_cflags)
test_update_refspec_endoflife_LDFLAGS = $(test_ldflags)
test_update_refspec_endoflife_LDADD = $(test_ldadd)
test_update_refspec_endoflife_SOURCES = test-update-refspec-endoflife.c

test_update_direct_CPPFLAGS = $(test_cppflags)
test_update_direct_CFLAGS = $(test_cflags)
test_update_direct_LDFLAGS = $(test_ldflags)
test_update_direct_LDADD = $(test_ldadd) $(top_builddir)/eos-updater/libeos-updater-dbus.la
test_update_direct_SOURCES = test-update-direct.c

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
