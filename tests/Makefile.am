testlib = libeos-updater-test.la
noinst_LTLIBRARIES = $(testlib)

libeos_updater_test_la_CPPFLAGS = \
	-DOSTREE_BINARY=\""$(OSTREE)"\" \
	-DGPG_BINARY=\""$(GPG)"\" \
	-DOSTREE_WITH_AUTOCLEANUPS \
	-I$(top_srcdir) \
	-I$(top_builddir) \
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
	$(NULL)

# make check-code-coverage rule
@CODE_COVERAGE_RULES@
CODE_COVERAGE_IGNORE_PATTERN = \
	eos-updater-generated.c \
	"*-autocleanups.h" \
	gmem.h \
	gobject.h \
	$(NULL)

check_PROGRAMS = \
	test-update-from-main \
	test-update-from-lan \
	test-update-from-volume \
	$(NULL)

TESTS = $(check_PROGRAMS)

AM_TESTS_ENVIRONMENT = \
	export \
	G_TEST_SRCDIR="$(abs_srcdir)" \
	G_TEST_BUILDDIR="$(abs_builddir)" \
	G_DEBUG=gc-friendly,fatal-criticals,fatal-warnings \
	MALLOC_CHECK_=3 \
	MALLOC_PERTURB_=$$(($${RANDOM:-256} % 256)) \
	OSTREE_SYSROOT_DEBUG=mutable-deployments \
	$(if $(EOS_UPDATER_GDB),EOS_CHECK_UPDATER_GDB_BASH_PATH=$(EOS_UPDATER_GDB)) \
	$(if $(EOS_UPDATE_SERVER_GDB_BASE),EOS_CHECK_UPDATE_SERVER_GDB_BASH_PATH_BASE=$(EOS_UPDATE_SERVER_GDB_BASE)) \
	$(if $(EOS_PREPARE_VOLUME_GDB),EOS_CHECK_PREPARE_VOLUME_GDB_BASH_PATH=$(EOS_PREPARE_VOLUME_GDB)) \
	;

test_cppflags = \
	-I$(top_srcdir) \
	-I$(top_builddir) \
	-I$(top_srcdir)/src \
	-I$(top_builddir)/src \
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

EXTRA_DIST = \
	gpghome/C1EB8F4E.asc \
	gpghome/keyid \
	gpghome/pubring.gpg \
	gpghome/random_seed \
	gpghome/secring.gpg \
	$(NULL)
