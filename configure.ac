AC_PREREQ(2.52)

AC_INIT(ModemManager, 0.3, dcbw@redhat.com, ModemManager)
AM_INIT_AUTOMAKE([1.9 subdir-objects tar-ustar no-dist-gzip dist-bzip2])
AM_MAINTAINER_MODE

AC_CONFIG_HEADERS(config.h)

dnl Required programs
AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_INSTALL
AC_PROG_LIBTOOL

PKG_CHECK_MODULES(MM, dbus-glib-1 >= 0.75 glib-2.0 >= 2.18 gmodule-2.0 gobject-2.0)

PKG_CHECK_MODULES(GUDEV, gudev-1.0)
AC_SUBST(GUDEV_CFLAGS)
AC_SUBST(GUDEV_LIBS)

AC_ARG_WITH(dbus-sys-dir, AS_HELP_STRING([--with-dbus-sys-dir=DIR], [where D-BUS system.d directory is]))

if test -n "$with_dbus_sys_dir" ; then
    DBUS_SYS_DIR="$with_dbus_sys_dir"
else
    DBUS_SYS_DIR="${sysconfdir}/dbus-1/system.d"
fi
AC_SUBST(DBUS_SYS_DIR)

AC_ARG_WITH(udev-base-dir, AS_HELP_STRING([--with-udev-base-dir=DIR], [where udev base directory is]))

if test -n "$with_udev_base_dir" ; then
    UDEV_BASE_DIR="$with_udev_base_dir"
else
    UDEV_BASE_DIR="/lib/udev"
fi
AC_SUBST(UDEV_BASE_DIR)

GLIB_GENMARSHAL=`pkg-config --variable=glib_genmarshal glib-2.0`
AC_SUBST(GLIB_GENMARSHAL)

# PPPD
AC_CHECK_HEADERS(pppd/pppd.h, have_pppd_headers="yes", have_pppd_headers="no")
AM_CONDITIONAL(HAVE_PPPD_H, test "x$have_pppd_headers" = "xyes")
case $have_pppd_headers in
    yes) ;;
    *)
        have_pppd_headers=no
        ;;
esac

AC_ARG_WITH([pppd-plugin-dir], AS_HELP_STRING([--with-pppd-plugin-dir=DIR], [path to the pppd plugins directory]))

if test -n "$with_pppd_plugin_dir" ; then
	PPPD_PLUGIN_DIR="$with_pppd_plugin_dir"
else
	PPPD_PLUGIN_DIR="${libdir}/pppd/2.4.4"
fi
AC_SUBST(PPPD_PLUGIN_DIR)

AC_ARG_ENABLE(more-warnings,
AS_HELP_STRING([--enable-more-warnings], [Maximum compiler warnings]), set_more_warnings="$enableval",set_more_warnings=yes)
AC_MSG_CHECKING(for more warnings, including -Werror)
if test "$GCC" = "yes" -a "$set_more_warnings" != "no"; then
	AC_MSG_RESULT(yes)
	CFLAGS="-Wall -Werror -std=gnu89 $CFLAGS"

	for option in -Wshadow -Wmissing-declarations -Wmissing-prototypes \
		      -Wdeclaration-after-statement -Wstrict-prototypes \
		      -Wfloat-equal -Wno-unused-parameter -Wno-sign-compare \
		      -fno-strict-aliasing; do
		SAVE_CFLAGS="$CFLAGS"
		CFLAGS="$CFLAGS $option"
		AC_MSG_CHECKING([whether gcc understands $option])
		AC_TRY_COMPILE([], [],
			has_option=yes,
			has_option=no)
		if test $has_option = no; then
			CFLAGS="$SAVE_CFLAGS"
		fi
		AC_MSG_RESULT($has_option)
		unset has_option
		unset SAVE_CFLAGS
	done
	unset option
else
	AC_MSG_RESULT(no)
fi

AC_ARG_WITH(docs, AC_HELP_STRING([--with-docs], [Build ModemManager documentation]))
AM_CONDITIONAL(WITH_DOCS, test "x$with_docs" = "xyes")
case $with_docs in
       yes) ;;
       *)
               with_docs=no
               ;;
esac

dnl
dnl Tests
dnl
AC_ARG_WITH(tests, AS_HELP_STRING([--with-tests], [Build ModemManager tests]))
AM_CONDITIONAL(WITH_TESTS, test "x$with_tests" = "xyes")
case $with_tests in
    yes)
        with_tests=yes
        ;;
    *)
        with_tests=no
        ;;
esac

AC_CONFIG_FILES([
Makefile
marshallers/Makefile
src/Makefile
src/tests/Makefile
plugins/Makefile
test/Makefile
introspection/Makefile
])
AC_OUTPUT

echo
echo Building with D-Bus system directory: ${DBUS_SYS_DIR}
echo
echo Building with udev base directory: ${UDEV_BASE_DIR}
echo
echo Building documentation: ${with_docs}
echo
echo Building PPP-enabled tests: ${have_pppd_headers}
echo

