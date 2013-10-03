AC_INIT([eos-updater],[0.1])
AC_CONFIG_MACRO_DIR([m4])
AC_USE_SYSTEM_EXTENSIONS
AM_INIT_AUTOMAKE([dist-xz no-dist-gzip])
AC_PROG_CC

# systemd unit support
AC_ARG_WITH([systemdsystemunitdir], AS_HELP_STRING([--with-systemdsystemunitdir=DIR],
	[Directory for systemd service files]))
# default location
AS_IF([test -z "$with_systemdsystemunitdir"],
	with_systemdsystemunitdir="\$(prefix)/lib/systemd/system")
AS_IF([test -z "$with_systemdsystemunitdir"], with_systemdsystemunitdir=no)
# add conditional and subst
AM_CONDITIONAL(HAVE_SYSTEMD, [test "$with_systemdsystemunitdir" != no])
if test "$with_systemdsystemunitdir" != no; then
	AC_SUBST([systemdsystemunitdir], [$with_systemdsystemunitdir])
fi

GLIB_REQUIRED_VERSION=2.36.0

PKG_CHECK_MODULES(EOS_UPDATER,
                  gio-unix-2.0 >= $GLIB_REQUIRED_VERSION)

PKG_CHECK_MODULES(EOS_UPDATER,
                  systemd >= 200)

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([
Makefile
src/Makefile
data/Makefile
])
AC_OUTPUT
