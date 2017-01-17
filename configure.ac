AC_INIT([eos-updater],[1.0])
AC_USE_SYSTEM_EXTENSIONS
AM_INIT_AUTOMAKE([1.11.2 dist-xz no-dist-gzip tar-ustar foreign])
AC_PROG_CC
AC_CONFIG_MACRO_DIR([m4])

PKG_PROG_PKG_CONFIG

# systemd unit support
AC_ARG_WITH([systemdsystemunitdir],
            AS_HELP_STRING([--with-systemdsystemunitdir=DIR], [Directory for systemd service files]),
            [],
            [with_systemdsystemunitdir=$($PKG_CONFIG --variable=systemdsystemunitdir systemd)])
AS_IF([test "x$with_systemdsystemunitdir" != "xno"], [
  AC_SUBST([systemdsystemunitdir], [$with_systemdsystemunitdir])
])

GLIB_REQUIRED_VERSION=2.44.0
OSTREE_REQUIRED_VERSION=2016.15

PKG_CHECK_MODULES([EOS_AUTOUPDATER],
                  [gio-unix-2.0 >= $GLIB_REQUIRED_VERSION
                  libnm-glib
                  libsystemd])

PKG_CHECK_MODULES([EOS_UPDATER],
                  [gio-unix-2.0 >= $GLIB_REQUIRED_VERSION
                  ostree-1 >= OSTREE_REQUIRED_VERSION
                  libsoup-2.4
                  eosmetrics-0])
AC_PATH_PROG([XSLTPROC], [xsltproc], [AC_MSG_ERROR([xsltproc required for build])])
AC_PATH_PROG([GDBUS_CODEGEN], [gdbus-codegen], [AC_MSG_ERROR([gdbus-codegen required for build])])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([
Makefile
src/Makefile
data/Makefile
])
AC_OUTPUT
