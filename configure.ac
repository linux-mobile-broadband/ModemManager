AC_PREREQ([2.63])

dnl The MM version number
m4_define([mm_major_version], [1])
m4_define([mm_minor_version], [4])
m4_define([mm_micro_version], [14])
m4_define([mm_version],
          [mm_major_version.mm_minor_version.mm_micro_version])

dnl libtool versioning for libmm-glib (-version-info c:r:a)
dnl    If the interface is unchanged, but the implementation has changed or
dnl        been fixed, then increment r.
dnl    Otherwise, increment c and zero r.
dnl        If the interface has grown (that is, the new library is compatible
dnl            with old code), increment a.
dnl        If the interface has changed in an incompatible way (that is,
dnl            functions have changed or been removed), then zero a.
m4_define([mm_glib_lt_current],  [2])
m4_define([mm_glib_lt_revision], [0])
m4_define([mm_glib_lt_age],      [2])


AC_INIT([ModemManager],[mm_version],[modemmanager-devel@lists.freedesktop.org],[ModemManager])
AM_INIT_AUTOMAKE([1.9 subdir-objects tar-ustar no-dist-gzip dist-xz -Wno-portability])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])
AM_MAINTAINER_MODE([enable])

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

dnl Version stuff
MM_MAJOR_VERSION=mm_major_version
MM_MINOR_VERSION=mm_minor_version
MM_MICRO_VERSION=mm_micro_version
MM_VERSION=mm_version
AC_SUBST(MM_MAJOR_VERSION)
AC_SUBST(MM_MINOR_VERSION)
AC_SUBST(MM_MICRO_VERSION)
AC_SUBST(MM_VERSION)

dnl libtool version stuff
MM_GLIB_LT_CURRENT=mm_glib_lt_current
MM_GLIB_LT_REVISION=mm_glib_lt_revision
MM_GLIB_LT_AGE=mm_glib_lt_age
AC_SUBST(MM_GLIB_LT_CURRENT)
AC_SUBST(MM_GLIB_LT_REVISION)
AC_SUBST(MM_GLIB_LT_AGE)

dnl
dnl Documentation
dnl
GTK_DOC_CHECK(1.0)

dnl
dnl translation support
dnl
IT_PROG_INTLTOOL([0.40.0])

AM_GNU_GETTEXT([external])
AM_GNU_GETTEXT_VERSION([0.17])

GETTEXT_PACKAGE=ModemManager
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE,"$GETTEXT_PACKAGE", [Gettext package])

PKG_CHECK_MODULES(MM,
                  glib-2.0 >= 2.32
                  gmodule-2.0
                  gobject-2.0
                  gio-2.0
                  gio-unix-2.0)
AC_SUBST(MM_CFLAGS)
AC_SUBST(MM_LIBS)

PKG_CHECK_MODULES(LIBMM_GLIB,
                  glib-2.0 >= 2.32
                  gobject-2.0
                  gio-2.0
                  gio-unix-2.0)
AC_SUBST(LIBMM_GLIB_CFLAGS)
AC_SUBST(LIBMM_GLIB_LIBS)

PKG_CHECK_MODULES(MMCLI,
                  glib-2.0 >= 2.32
                  gobject-2.0
                  gio-2.0)
AC_SUBST(MMCLI_CFLAGS)
AC_SUBST(LIBMM_LIBS)

PKG_CHECK_MODULES(GUDEV, gudev-1.0 >= 147)
AC_SUBST(GUDEV_CFLAGS)
AC_SUBST(GUDEV_LIBS)

# Some required utilities
GLIB_MKENUMS=`$PKG_CONFIG --variable=glib_mkenums glib-2.0`
AC_SUBST(GLIB_MKENUMS)
GDBUS_CODEGEN=`$PKG_CONFIG --variable=gdbus_codegen gio-2.0`
AC_SUBST(GDBUS_CODEGEN)

# DBus testing support only with GIO 2.34
PKG_CHECK_MODULES(DBUS_TEST_SUPPORT, [gio-2.0 >= 2.34], [have_dbus_test_support=yes], [have_dbus_test_support=no])
AM_CONDITIONAL(BUILD_DBUS_TEST_SUPPORT, test "x$have_dbus_test_support" = "xyes")

# GObject Introspection
GOBJECT_INTROSPECTION_CHECK([0.9.6])

# Vala bindings
VAPIGEN_CHECK(0.18)

# Sanity check
if test "x$enable_vala" = "xyes" -a ! -f "$VAPIGEN_MAKEFILE"; then
  AC_MSG_ERROR([Vala bindings enabled but Makefile.vapigen not found. Install vala-devel, or pass --disable-vala])
fi

# DBus system directory
AC_ARG_WITH(dbus-sys-dir, AS_HELP_STRING([--with-dbus-sys-dir=DIR], [where D-BUS system.d directory is]))
if test -n "$with_dbus_sys_dir" ; then
    DBUS_SYS_DIR="$with_dbus_sys_dir"
else
    DBUS_SYS_DIR="${sysconfdir}/dbus-1/system.d"
fi
AC_SUBST(DBUS_SYS_DIR)

# udev base directory
AC_ARG_WITH(udev-base-dir, AS_HELP_STRING([--with-udev-base-dir=DIR], [where udev base directory is]))
if test -n "$with_udev_base_dir" ; then
    UDEV_BASE_DIR="$with_udev_base_dir"
else
    UDEV_BASE_DIR="/lib/udev"
fi
AC_SUBST(UDEV_BASE_DIR)

# systemd system unit directory
AC_ARG_WITH([systemdsystemunitdir], AS_HELP_STRING([--with-systemdsystemunitdir=DIR], [where systemd service files are]),
            [], [with_systemdsystemunitdir=$($PKG_CONFIG --variable=systemdsystemunitdir systemd)])
if test "x$with_systemdsystemunitdir" != xno; then
    AC_SUBST([SYSTEMD_UNIT_DIR],  [$with_systemdsystemunitdir])
fi
AM_CONDITIONAL(HAVE_SYSTEMD, [test -n "$SYSTEMD_UNIT_DIR" -a "$SYSTEMD_UNIT_DIR" != xno ])

# PolicyKit
PKG_CHECK_MODULES(POLKIT, [polkit-gobject-1 >= 0.97], [have_polkit=yes],[have_polkit=no])
AC_ARG_WITH(polkit,
            AS_HELP_STRING([--with-polkit=(strict|permissive|none)],
                           [Enable PolicyKit support [[default=auto]]]),,
            [with_polkit=auto])
# Handle 'auto' ('strict' if polkit found, 'none' otherwise),
#  'yes' ('strict') and 'no' ('none')
if test "x$with_polkit" = "xauto"; then
	if test "x$have_polkit" = "xno"; then
        with_polkit="none"
    else
        with_polkit="strict"
    fi
elif test "x$with_polkit" = "xno"; then
    with_polkit=none
elif test "x$with_polkit" = "xyes"; then
    with_polkit=strict
fi
# Build policies context
if test "x$with_polkit" = "xnone"; then
    AC_DEFINE(WITH_POLKIT, 0, [Define if you have PolicyKit support])
else
	if test "x$have_polkit" = "xno"; then
		AC_MSG_ERROR(PolicyKit development headers are required)
	fi

    case "x$with_polkit" in
        "xpermissive")
            MM_DEFAULT_USER_POLICY="yes"
            ;;
        "xstrict")
            MM_DEFAULT_USER_POLICY="auth_self_keep"
            ;;
        *)
            AC_MSG_ERROR([Wrong value for --with-polkit: $with_polkit])
            ;;
    esac

    AC_DEFINE(WITH_POLKIT, 1, [Define if you have PolicyKit support])
    AC_SUBST(POLKIT_CFLAGS)
    AC_SUBST(POLKIT_LIBS)
    AC_SUBST(MM_DEFAULT_USER_POLICY)
fi

AM_CONDITIONAL(WITH_POLKIT, [test "x$with_polkit" != "xnone" ])

dnl
dnl MBIM support (enabled by default)
dnl
AC_ARG_WITH(mbim, AS_HELP_STRING([--without-mbim], [Build without MBIM support]), [], [with_mbim=yes])
AM_CONDITIONAL(WITH_MBIM, test "x$with_mbim" = "xyes")
case $with_mbim in
    yes)
        PKG_CHECK_MODULES(MBIM, [mbim-glib >= 1.10], [have_mbim=yes],[have_mbim=no])
        if test "x$have_mbim" = "xno"; then
            AC_MSG_ERROR([Couldn't find libmbim-glib. Install it, or otherwise configure using --without-mbim to disable MBIM support.])
        else
            AC_DEFINE(WITH_MBIM, 1, [Define if you want MBIM support])
            AC_SUBST(MBIM_CFLAGS)
            AC_SUBST(MBIM_LIBS)
        fi
        ;;
    *)
        with_mbim=no
        ;;
esac

dnl
dnl QMI support (enabled by default)
dnl
AC_ARG_WITH(qmi, AS_HELP_STRING([--without-qmi], [Build without QMI support]), [], [with_qmi=yes])
AM_CONDITIONAL(WITH_QMI, test "x$with_qmi" = "xyes")
case $with_qmi in
    yes)
        PKG_CHECK_MODULES(QMI, [qmi-glib >= 1.12.4], [have_qmi=yes],[have_qmi=no])
        if test "x$have_qmi" = "xno"; then
            AC_MSG_ERROR([Couldn't find libqmi-glib. Install it, or otherwise configure using --without-qmi to disable QMI support.])
        else
            dnl DMS Set FCC Authentication set in 1.12.4 (stable) and 1.13.4
            dnl small additional check to make sure we're not in an unstable 1.13.x version without the fix.
            AC_CHECK_LIB([qmi-glib],
                         [qmi_client_dms_set_fcc_authentication],
                         [have_set_fcc_authentication=yes],
                         [have_set_fcc_authentication=no])
            if test "x$have_set_fcc_authentication" = "xno"; then
               AC_MSG_ERROR([libqmi doesn't have "DMS Set FCC Authentication" support])
            fi
            AC_DEFINE(WITH_QMI, 1, [Define if you want QMI support])
            AC_SUBST(QMI_CFLAGS)
            AC_SUBST(QMI_LIBS)
        fi
        ;;
    *)
        with_qmi=no
        ;;
esac

dnl
dnl Newest QMI commands
dnl
AC_ARG_WITH(newest_qmi_commands, AS_HELP_STRING([--with-newest-qmi-commands], [Try to avoid deprecated QMI commands]))
AM_CONDITIONAL(WITH_NEWEST_QMI_COMMANDS, test "x$with_newest_qmi_commands" = "xyes")
case $with_newest_qmi_commands in
    yes)
        if test "x$with_qmi" = "xno" ; then
           AC_ERROR([Cannot enable newest QMI commands if QMI support is disabled])
        fi

        AC_DEFINE(WITH_NEWEST_QMI_COMMANDS, 1, [Define if we enable new QMI commands support])
        with_newest_qmi_commands=yes
        ;;
    *)
        with_newest_qmi_commands=no
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

# Not building protocol libs standalone
AM_CONDITIONAL(QCDM_STANDALONE, test "yes" = "no")
AM_CONDITIONAL(WMC_STANDALONE, test "yes" = "no")

AC_CONFIG_FILES([
Makefile
data/Makefile
data/ModemManager.pc
data/mm-glib.pc
data/org.freedesktop.ModemManager1.policy.in
data/tests/Makefile
data/tests/org.freedesktop.ModemManager1.service
include/Makefile
include/ModemManager-version.h
build-aux/Makefile
libqcdm/Makefile
libqcdm/src/Makefile
libqcdm/tests/Makefile
libwmc/Makefile
libwmc/src/Makefile
libwmc/tests/Makefile
src/Makefile
src/tests/Makefile
plugins/Makefile
uml290/Makefile
test/Makefile
introspection/Makefile
introspection/tests/Makefile
po/Makefile.in
docs/Makefile
docs/man/Makefile
docs/reference/Makefile
docs/reference/api/Makefile
docs/reference/api/version.xml
docs/reference/libmm-glib/Makefile
docs/reference/libmm-glib/version.xml
libmm-glib/Makefile
libmm-glib/generated/Makefile
libmm-glib/generated/tests/Makefile
libmm-glib/tests/Makefile
vapi/Makefile
cli/Makefile
examples/Makefile
examples/modem-watcher-python/Makefile
examples/modem-watcher-javascript/Makefile
])
AC_OUTPUT

echo "
    ModemManager $VERSION
    ==============================================

    compiler:                ${CC}
    cflags:                  ${CFLAGS}
    Maintainer mode:         ${USE_MAINTAINER_MODE}

    D-Bus system directory:  ${DBUS_SYS_DIR}
    udev base directory:     ${UDEV_BASE_DIR}
    systemd unit directory:  ${with_systemdsystemunitdir}

    PolicyKit support:       ${with_polkit}
    GObject Introspection:   ${found_introspection}
    Vala Bindings:           ${enable_vala}
    Documentation:           ${enable_gtk_doc}
    MBIM support:            ${with_mbim}
    QMI support:             ${with_qmi}
    Newest QMI commands:     ${with_newest_qmi_commands}
"
