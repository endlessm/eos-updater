ACLOCAL_AMFLAGS = ${ACLOCAL_FLAGS} -I m4

SUBDIRS = . src data tests docs

# Pre-defines to allow appending later
noinst_LTLIBRARIES =

EXTRA_DIST = autogen.sh README.md

# Ensure systemd units get installed under $(prefix) for distcheck
AM_DISTCHECK_CONFIGURE_FLAGS = --with-systemdsystemunitdir='$${libdir}/systemd/system' --enable-introspection

pkgconfigdir = $(libdir)/pkgconfig

pkgconfig_DATA = eos-updater-0.pc

# Utility library
noinst_LTLIBRARIES += libeos-updater-util/libeos-updater-util-@EUU_API_VERSION@.la

libeos_updater_util_libeos_updater_util_@EUU_API_VERSION@_la_CFLAGS = \
	$(CODE_COVERAGE_CFLAGS) \
	$(WARN_CFLAGS) \
	$(GIO_CFLAGS) \
	$(OSTREE_CFLAGS) \
	$(SOUP_CFLAGS) \
	$(NULL)
libeos_updater_util_libeos_updater_util_@EUU_API_VERSION@_la_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_builddir) \
	-DOSTREE_WITH_AUTOCLEANUPS \
	-DSYSCONFDIR=\""$(sysconfdir)"\" \
	-DLOCALSTATEDIR=\""$(localstatedir)"\" \
	-DDATADIR=\""$(datadir)"\" \
	-DPKGDATADIR=\""$(pkgdatadir)"\" \
	-DPREFIX=\""$(prefix)"\" \
	$(NULL)
libeos_updater_util_libeos_updater_util_@EUU_API_VERSION@_la_LDFLAGS = \
	$(WARN_LDFLAGS) \
	$(NULL)
libeos_updater_util_libeos_updater_util_@EUU_API_VERSION@_la_LIBADD = \
	$(CODE_COVERAGE_LIBS) \
	$(GIO_LIBS) \
	$(OSTREE_LIBS) \
	$(SOUP_LIBS) \
	$(NULL)
libeos_updater_util_libeos_updater_util_@EUU_API_VERSION@_la_SOURCES = \
	libeos-updater-util/util.c \
	libeos-updater-util/util.h \
	libeos-updater-util/refcounted.h \
	libeos-updater-util/extensions.c \
	libeos-updater-util/extensions.h \
	$(NULL)
