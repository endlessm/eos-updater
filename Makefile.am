ACLOCAL_AMFLAGS = ${ACLOCAL_FLAGS} -I m4
AM_CFLAGS = ${WARN_CFLAGS}

SUBDIRS = src data tests docs

EXTRA_DIST = autogen.sh README.md

# Ensure systemd units get installed under $(prefix) for distcheck
AM_DISTCHECK_CONFIGURE_FLAGS = --with-systemdsystemunitdir='$${libdir}/systemd/system' --enable-introspection

pkgconfigdir = $(libdir)/pkgconfig

pkgconfig_DATA = eos-updater-0.pc
