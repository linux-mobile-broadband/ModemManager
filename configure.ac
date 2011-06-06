AC_PREREQ([2.60])

AC_INIT([ModemManager],[0.4.997],[dcbw@redhat.com],[ModemManager])
AM_INIT_AUTOMAKE([1.9 subdir-objects tar-ustar no-dist-gzip dist-bzip2 -Wno-portability])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])
AM_MAINTAINER_MODE

AC_CONFIG_MACRO_DIR([m4])

AC_CONFIG_HEADERS(config.h)

dnl Define system extensions for various things like strcasestr()
AC_USE_SYSTEM_EXTENSIONS

dnl Required programs
AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_INSTALL

dnl Initialize libtool
LT_PREREQ([2.2])
LT_INIT

dnl
dnl translation support
dnl
GETTEXT_PACKAGE=ModemManager
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE,"$GETTEXT_PACKAGE", [Gettext package])
IT_PROG_INTLTOOL([0.35.0])

PKG_CHECK_MODULES(MM, dbus-glib-1 >= 0.86 glib-2.0 >= 2.18 gmodule-2.0 gobject-2.0)

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

# PolicyKit
AC_ARG_WITH(polkit, AS_HELP_STRING([--with-polkit], [Build with PolicyKit support]))
AM_CONDITIONAL(WITH_POLKIT, test "x$with_polkit" = "xyes")
case $with_polkit in
    yes)
        with_polkit=yes
	PKG_CHECK_MODULES(POLKIT, polkit-gobject-1 >= 0.95)
	AC_DEFINE(WITH_POLKIT, 1, [Define if you want to use PolicyKit])
	AC_SUBST(POLKIT_CFLAGS)
	AC_SUBST(POLKIT_LIBS)

	# Check for polkit_authority_get_sync()
	AC_CHECK_LIB([polkit-gobject-1], [polkit_authority_get_sync], ac_have_pk_auth_get_sync="1", ac_have_pk_auth_get_sync="0")
	AC_DEFINE_UNQUOTED(HAVE_POLKIT_AUTHORITY_GET_SYNC, $ac_have_pk_auth_get_sync, [Define if you have a polkit with polkit_authority_get_sync()])

	AM_GLIB_GNU_GETTEXT
        ;;
    *)
        with_polkit=no
        ;;
esac

dnl
dnl Checks for new dbus-glib property access function
dnl
AC_CHECK_LIB([dbus-glib-1], [dbus_glib_global_set_disable_legacy_property_access], ac_have_dg_prop="1", ac_have_dg_prop="0")
AC_DEFINE_UNQUOTED(HAVE_DBUS_GLIB_DISABLE_LEGACY_PROP_ACCESS, $ac_have_dg_prop, [Define if you have a dbus-glib with dbus_glib_global_set_disable_legacy_property_access()])

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
	PPPD_PLUGIN_DIR="${libdir}/pppd/2.4.5"
fi
AC_SUBST(PPPD_PLUGIN_DIR)

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

NM_COMPILER_WARNINGS


dnl
dnl Distribution version string
dnl
AC_ARG_WITH(dist-version, AS_HELP_STRING([--with-dist-version=<mm-dist-version>], [Define the custom version (like distribution package name and revision)]), ac_distver=$withval, ac_distver="")
if ! test x"$ac_distver" = x""; then
  AC_DEFINE_UNQUOTED(MM_DIST_VERSION, "$ac_distver", [Define the distribution version string])
fi

AC_CONFIG_FILES([
Makefile
marshallers/Makefile
libqcdm/Makefile
libqcdm/src/Makefile
libqcdm/tests/Makefile
src/Makefile
src/tests/Makefile
plugins/Makefile
test/Makefile
introspection/Makefile
po/Makefile.in
policy/Makefile
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
echo Building with PolicyKit support: ${with_polkit}
echo

