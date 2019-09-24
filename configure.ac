dnl Copyright © 2017 Endless Mobile, Inc.
dnl
dnl This library is free software; you can redistribute it and/or
dnl modify it under the terms of the GNU Lesser General Public
dnl License as published by the Free Software Foundation; either
dnl version 2.1 of the License, or (at your option) any later version.
dnl
dnl This library is distributed in the hope that it will be useful,
dnl but WITHOUT ANY WARRANTY; without even the implied warranty of
dnl MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
dnl Lesser General Public License for more details.
dnl
dnl You should have received a copy of the GNU Lesser General Public
dnl License along with this library; if not, write to the Free Software
dnl Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

# API versions
m4_define([eu_api_version],[0])
m4_define([euu_api_version],[0])
m4_define([eufi_api_version],[0])
m4_define([eus_api_version],[0])

AC_INIT([eos-updater],[1.0])
AC_CONFIG_AUX_DIR([build-aux])
AC_USE_SYSTEM_EXTENSIONS
AM_INIT_AUTOMAKE([1.11.2 dist-xz no-dist-gzip tar-ustar foreign subdir-objects
                  -Wno-portability])
AM_SILENT_RULES([yes])
AC_PROG_CC
AC_CONFIG_MACRO_DIR([m4])
LT_INIT
AX_IS_RELEASE([git-directory])
AC_REQUIRE_AUX_FILE([tap-driver.sh])

PKG_PROG_PKG_CONFIG

GTK_DOC_CHECK([1.14],[--flavour no-tmpl])

GOBJECT_INTROSPECTION_CHECK([1.30.0])

# Versioning
AC_SUBST([EU_API_VERSION],eu_api_version)
AC_SUBST([EUU_API_VERSION],euu_api_version)
AC_SUBST([EUFI_API_VERSION],eufi_api_version)
AC_SUBST([EUS_API_VERSION],eus_api_version)

# Compiler warnings and --disable-Werror
AX_COMPILER_FLAGS([WARN_CFLAGS],[WARN_LDFLAGS],,,[-Wno-declaration-after-statement -Wconversion])

# Enable C99
AX_APPEND_COMPILE_FLAGS([-std=c99],[STD_CFLAGS])
AC_SUBST([STD_CFLAGS])

# Valgrind support for tests
AX_VALGRIND_CHECK

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
            [port=43381])
AC_SUBST([server_port], [$port])
AC_DEFINE_UNQUOTED([EOS_AVAHI_PORT], [$port], [Socket activation port to be used by eos-update-server])

GLIB_REQUIRED_VERSION=2.62.0
OSTREE_REQUIRED_VERSION=2018.6
AVAHI_REQUIRED_VERSION=0.6.31
NM_REQUIRED_VERSION=1.2.0
FLATPAK_REQUIRED_VERSION=1.1.2
JSON_GLIB_REQUIRED_VERSION=1.2.6

PKG_CHECK_MODULES([FLATPAK],
                  [flatpak >= $FLATPAK_REQUIRED_VERSION])

PKG_CHECK_MODULES([GIO],
                  [gio-unix-2.0 >= $GLIB_REQUIRED_VERSION])

PKG_CHECK_MODULES([JSON_GLIB],
                  [json-glib-1.0 >= $JSON_GLIB_REQUIRED_VERSION])

PKG_CHECK_MODULES([OSTREE],
                  [ostree-1 >= $OSTREE_REQUIRED_VERSION])

PKG_CHECK_MODULES([SOUP],
                  [libsoup-2.4])

AX_PKG_CHECK_MODULES([EOS_AUTOUPDATER],[glib-2.0 >= $GLIB_REQUIRED_VERSION gio-2.0 gobject-2.0 libnm >= $NM_REQUIRED_VERSION])
AX_PKG_CHECK_MODULES([EOS_UPDATE_SERVER],[glib-2.0 >= $GLIB_REQUIRED_VERSION gio-2.0 gobject-2.0 ostree-1 >= $OSTREE_REQUIRED_VERSION libsoup-2.4 libsystemd])
AX_PKG_CHECK_MODULES([EOS_UPDATER_AVAHI],[glib-2.0 >= $GLIB_REQUIRED_VERSION gio-2.0 gobject-2.0 ostree-1 >= $OSTREE_REQUIRED_VERSION])
AX_PKG_CHECK_MODULES([EOS_UPDATER_UTIL_TESTS],[flatpak >= $FLATPAK_REQUIRED_VERSION glib-2.0 >= $GLIB_REQUIRED_VERSION gio-2.0 gobject-2.0 json-glib-1.0 >= $JSON_GLIB_REQUIRED_VERSION ostree-1 >= $OSTREE_REQUIRED_VERSION libsoup-2.4])
AX_PKG_CHECK_MODULES([EOS_UPDATER_FLATPAK_INSTALLER_TESTS],[flatpak >= $FLATPAK_REQUIRED_VERSION glib-2.0 >= $GLIB_REQUIRED_VERSION gio-2.0 gobject-2.0 json-glib-1.0 >= $JSON_GLIB_REQUIRED_VERSION ostree-1 >= $OSTREE_REQUIRED_VERSION])
AX_PKG_CHECK_MODULES([EOS_UPDATER_FLATPAK_INSTALLER],[flatpak >= $FLATPAK_REQUIRED_VERSION glib-2.0 >= $GLIB_REQUIRED_VERSION gio-2.0 gobject-2.0 json-glib-1.0 >= $JSON_GLIB_REQUIRED_VERSION ostree-1 >= $OSTREE_REQUIRED_VERSION])

EOS_UPDATER_MODULES="glib-2.0 >= $GLIB_REQUIRED_VERSION gio-2.0 gobject-2.0 gio-unix-2.0 >= $GLIB_REQUIRED_VERSION ostree-1 >= $OSTREE_REQUIRED_VERSION flatpak >= $FLATPAK_REQUIRED_VERSION avahi-client >= $AVAHI_REQUIRED_VERSION avahi-glib >= $AVAHI_REQUIRED_VERSION mogwai-schedule-client-0 libsoup-2.4"

AS_IF([test "x$want_metrics" = 'xyes'],
      [EOS_UPDATER_MODULES="eosmetrics-0 $EOS_UPDATER_MODULES"
       AC_DEFINE([HAS_EOSMETRICS_0], [1], [Has eosmetrics-0 library])])
AX_PKG_CHECK_MODULES([EOS_UPDATER],[$EOS_UPDATER_MODULES])

AC_PATH_PROG([GDBUS_CODEGEN],[gdbus-codegen])
AS_IF([test "$GDBUS_CODEGEN" = ""],[
	AC_MSG_ERROR([gdbus-codegen is required for build])
])

AC_PATH_PROG([OSTREE],[ostree])
AS_IF([test "$OSTREE" = ""],[
	AC_MSG_ERROR([ostree command line tool is required for tests])
])

AC_PATH_PROG([GPG],[gpg])
AS_IF([test "$GPG" = ""],[
	AC_MSG_ERROR([gpg command line tool is required for tests])
])

AC_PATH_PROG([FLATPAK],[flatpak])
AS_IF([test "$FLATPAK" = ""],[
	AC_MSG_ERROR([flatpak command line tool is required for tests])
])

AC_PATH_PROG([GLIB_COMPILE_RESOURCES],[glib-compile-resources])
AS_IF([test "$GLIB_COMPILE_RESOURCES" == ""],[
	AC_MSG_ERROR([glib-compile-resources not found])
])

AC_PATH_PROG([GLIB_MKENUMS],[glib-mkenums])
AS_IF([test "$GLIB_MKENUMS" == ""],[
	AC_MSG_ERROR([glib-mkenums not found])
])

# Code coverage
AX_CODE_COVERAGE

# installed-tests
AC_ARG_ENABLE([always_build_tests],
              AS_HELP_STRING([--enable-always-build-tests],
                             [Enable always building tests (default: yes)]),,
              [enable_always_build_tests=yes])
AC_ARG_ENABLE([installed_tests],
              AS_HELP_STRING([--enable-installed-tests],
                             [Install test programs (default: no)]),,
              [enable_installed_tests=no])

AM_CONDITIONAL([ENABLE_ALWAYS_BUILD_TESTS],
               [test "$enable_always_build_tests" = "yes"])
AC_SUBST([ENABLE_ALWAYS_BUILD_TESTS],[$enable_always_build_tests])

AM_CONDITIONAL([ENABLE_INSTALLED_TESTS],
               [test "$enable_installed_tests" = "yes"])
AC_SUBST([ENABLE_INSTALLED_TESTS],[$enable_installed_tests])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([
Makefile
libeos-updater-flatpak-installer/tests/Makefile
libeos-updater-util/tests/Makefile
tests/Makefile
])
AC_OUTPUT
