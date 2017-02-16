ACLOCAL_AMFLAGS = ${ACLOCAL_FLAGS} -I m4

SUBDIRS = . src libeos-updater-util/tests data tests docs

# Pre-defines to allow appending later
noinst_LTLIBRARIES =
EXTRA_DIST =
CLEANFILES =
DISTCLEANFILES =
MAINTAINERCLEANFILES =

# Documentation
EXTRA_DIST += README.md

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
	libeos-updater-util/extensions.c \
	libeos-updater-util/extensions.h \
	libeos-updater-util/refcounted.h \
	libeos-updater-util/util.c \
	libeos-updater-util/util.h \
	$(NULL)

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
