
ACLOCAL_AMFLAGS = ${ACLOCAL_FLAGS}
AM_CFLAGS = ${WARN_CFLAGS}

SUBDIRS = src data

EXTRA_DIST = autogen.sh README.md

# Ensure systemd units get installed under $(prefix) for distcheck
DISTCHECK_CONFIGURE_FLAGS = --with-systemdsystemunitdir='$${libdir}/systemd/system'
