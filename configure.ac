AC_INIT([eos-updater],[1.0])
AC_USE_SYSTEM_EXTENSIONS
AM_INIT_AUTOMAKE([1.11.2 dist-xz no-dist-gzip tar-ustar foreign])
AM_SILENT_RULES([yes])
AC_PROG_CC
AC_CONFIG_MACRO_DIR([m4])
LT_INIT

PKG_PROG_PKG_CONFIG

# systemd unit support
AC_ARG_WITH([systemdsystemunitdir],
            AS_HELP_STRING([--with-systemdsystemunitdir=DIR], [Directory for systemd service files]),
            [],
            [with_systemdsystemunitdir=$($PKG_CONFIG --variable=systemdsystemunitdir systemd)])
AS_IF([test "x$with_systemdsystemunitdir" != "xno"], [
  AC_SUBST([systemdsystemunitdir], [$with_systemdsystemunitdir])
])

# build updater with metrics
AC_ARG_ENABLE([metrics],
              AS_HELP_STRING([--disable-metrics], [Whether to build eos-updater with metrics support]),
              [want_metrics=$withval],
              [want_metrics=yes])
AM_CONDITIONAL([WITH_METRICS], [test "x$want_metrics" = 'xyes'])

# port used for eos-server
AC_ARG_WITH([server-port],
            AS_HELP_STRING([--with-server-port], [Port number for the update server]),
            [port=$withval],
            [port=12345])
AC_SUBST([server_port], [$port])
AC_DEFINE_UNQUOTED([EOS_AVAHI_PORT], [$port], [Socket activation port to be used by eos-update-server])

GLIB_REQUIRED_VERSION=2.44.0
OSTREE_REQUIRED_VERSION=2016.15
AVAHI_REQUIRED_VERSION=0.6.31

PKG_CHECK_MODULES([GIO],
                  [gio-unix-2.0 >= $GLIB_REQUIRED_VERSION])

PKG_CHECK_MODULES([OSTREE],
                  [ostree-1 >= OSTREE_REQUIRED_VERSION])

PKG_CHECK_MODULES([SOUP],
                  [libsoup-2.4])

PKG_CHECK_MODULES([EOS_AUTOUPDATER],
                  [libnm-glib
                   libsystemd])

PKG_CHECK_MODULES([EOS_UPDATE_SERVER],
                  [libsystemd])

EOS_UPDATER_MODULES="avahi-client >= $AVAHI_REQUIRED_VERSION avahi-glib >= $AVAHI_REQUIRED_VERSION"

AS_IF([test "x$want_metrics" = 'xyes'],
      [EOS_UPDATER_MODULES="eosmetrics-0 $EOS_UPDATER_MODULES"
       AC_DEFINE([HAS_EOSMETRICS_0], [1], [Has eosmetrics-0 library])])
PKG_CHECK_MODULES([EOS_UPDATER],
                  [$EOS_UPDATER_MODULES])

AC_PATH_PROG([XSLTPROC], [xsltproc], [AC_MSG_ERROR([xsltproc required for build])])
AC_PATH_PROG([GDBUS_CODEGEN], [gdbus-codegen], [AC_MSG_ERROR([gdbus-codegen required for build])])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([
Makefile
src/Makefile
data/Makefile
])
AC_OUTPUT
