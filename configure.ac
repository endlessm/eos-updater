AC_INIT([eos-updater],[1.0])
AC_USE_SYSTEM_EXTENSIONS
AM_INIT_AUTOMAKE([dist-xz no-dist-gzip tar-ustar foreign])
AC_PROG_CC

PKG_PROG_PKG_CONFIG

# systemd unit support
AC_ARG_WITH([systemdsystemunitdir],
            AS_HELP_STRING([--with-systemdsystemunitdir=DIR], [Directory for systemd service files]),
            [],
            [with_systemdsystemunitdir=$($PKG_CONFIG --variable=systemdsystemunitdir systemd)])
AS_IF([test "x$with_systemdsystemunitdir" != "xno"], [
  AC_SUBST([systemdsystemunitdir], [$with_systemdsystemunitdir])
])

GJS_CONSOLE=`$PKG_CONFIG --variable=gjs_console gjs-1.0`
AC_SUBST(GJS_CONSOLE)

GLIB_REQUIRED_VERSION=2.36.0

PKG_CHECK_MODULES(EOS_UPDATER,
                  gio-unix-2.0 >= $GLIB_REQUIRED_VERSION
                  libnm-glib
                  libsystemd-journal)

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([
Makefile
src/Makefile
data/Makefile
])
AC_OUTPUT
