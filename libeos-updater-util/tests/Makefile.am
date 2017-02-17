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
	$(NULL)

avahi_service_file_SOURCES = avahi-service-file.c
config_SOURCES = config.c
ostree_SOURCES = ostree.c

-include $(top_srcdir)/git.mk
