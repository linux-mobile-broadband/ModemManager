AC_PREREQ([2.63])

dnl-----------------------------------------------------------------------------
dnl Package and library versioning support
dnl

m4_define([mm_major_version], [1])
m4_define([mm_minor_version], [6])
m4_define([mm_micro_version], [0])
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
m4_define([mm_glib_lt_current],  [3])
m4_define([mm_glib_lt_revision], [0])
m4_define([mm_glib_lt_age],      [3])

dnl-----------------------------------------------------------------------------
dnl autoconf, automake, libtool initialization
dnl
AC_INIT([ModemManager],[mm_version],[modemmanager-devel@lists.freedesktop.org],[ModemManager])
AM_INIT_AUTOMAKE([1.11 subdir-objects tar-ustar no-dist-gzip dist-xz -Wno-portability])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AM_MAINTAINER_MODE([enable])

AM_SILENT_RULES([yes])

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
LT_INIT([disable-static])

dnl-----------------------------------------------------------------------------
dnl Version definitions
dnl

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

dnl-----------------------------------------------------------------------------
dnl Documentation
dnl

GTK_DOC_CHECK(1.0)

dnl-----------------------------------------------------------------------------
dnl i18n
dnl

IT_PROG_INTLTOOL([0.40.0])

AM_GNU_GETTEXT([external])
AM_GNU_GETTEXT_VERSION([0.19.3])

GETTEXT_PACKAGE=ModemManager
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE,"$GETTEXT_PACKAGE", [Gettext package])

dnl-----------------------------------------------------------------------------
dnl Build dependencies
dnl

GLIB_MIN_VERSION=2.36.0
GUDEV_MIN_VERSION=147

PKG_CHECK_MODULES(MM,
                  glib-2.0 >= $GLIB_MIN_VERSION
                  gmodule-2.0
                  gobject-2.0
                  gio-2.0
                  gio-unix-2.0)
AC_SUBST(MM_CFLAGS)
AC_SUBST(MM_LIBS)

PKG_CHECK_MODULES(LIBMM_GLIB,
                  glib-2.0 >= $GLIB_MIN_VERSION
                  gobject-2.0
                  gio-2.0
                  gio-unix-2.0)
AC_SUBST(LIBMM_GLIB_CFLAGS)
AC_SUBST(LIBMM_GLIB_LIBS)

PKG_CHECK_MODULES(MMCLI,
                  glib-2.0 >= $GLIB_MIN_VERSION
                  gobject-2.0
                  gio-2.0)
AC_SUBST(MMCLI_CFLAGS)
AC_SUBST(MMCLI_LIBS)

PKG_CHECK_MODULES(GUDEV, gudev-1.0 >= $GUDEV_MIN_VERSION)
AC_SUBST(GUDEV_CFLAGS)
AC_SUBST(GUDEV_LIBS)

dnl Some required utilities
GLIB_MKENUMS=`$PKG_CONFIG --variable=glib_mkenums glib-2.0`
AC_SUBST(GLIB_MKENUMS)

GDBUS_CODEGEN=`$PKG_CONFIG --variable=gdbus_codegen gio-2.0`
AC_SUBST(GDBUS_CODEGEN)

dnl-----------------------------------------------------------------------------
dnl Testing support
dnl

dnl Code coverage (disabled by default)
AX_CODE_COVERAGE

dnl-----------------------------------------------------------------------------
dnl Introspection and bindings
dnl

dnl GObject Introspection
GOBJECT_INTROSPECTION_CHECK([0.9.6])

dnl Vala bindings
VAPIGEN_CHECK(0.18)
if test "x$enable_vala" = "xyes" -a ! -f "$VAPIGEN_MAKEFILE"; then
  AC_MSG_ERROR([Vala bindings enabled but Makefile.vapigen not found. Install vala-devel, or pass --disable-vala])
fi

dnl-----------------------------------------------------------------------------
dnl System paths
dnl

dnl DBus system directory
AC_ARG_WITH(dbus-sys-dir, AS_HELP_STRING([--with-dbus-sys-dir=DIR], [where D-BUS system.d directory is]))
if test -n "$with_dbus_sys_dir" ; then
    DBUS_SYS_DIR="$with_dbus_sys_dir"
else
    DBUS_SYS_DIR="${sysconfdir}/dbus-1/system.d"
fi
AC_SUBST(DBUS_SYS_DIR)

dnl udev base directory
AC_ARG_WITH(udev-base-dir, AS_HELP_STRING([--with-udev-base-dir=DIR], [where udev base directory is]))
if test -n "$with_udev_base_dir" ; then
    UDEV_BASE_DIR="$with_udev_base_dir"
else
    UDEV_BASE_DIR="/lib/udev"
fi
AC_SUBST(UDEV_BASE_DIR)

dnl systemd system unit directory
AC_ARG_WITH([systemdsystemunitdir], AS_HELP_STRING([--with-systemdsystemunitdir=DIR], [where systemd service files are]),
            [], [with_systemdsystemunitdir=$($PKG_CONFIG --variable=systemdsystemunitdir systemd)])
if test "x$with_systemdsystemunitdir" != xno; then
    AC_SUBST([SYSTEMD_UNIT_DIR],  [$with_systemdsystemunitdir])
fi
AM_CONDITIONAL(HAVE_SYSTEMD, [test -n "$SYSTEMD_UNIT_DIR" -a "$SYSTEMD_UNIT_DIR" != xno ])

dnl-----------------------------------------------------------------------------
dnl Suspend/resume support
dnl

PKG_CHECK_MODULES(LIBSYSTEMD, [libsystemd >= 209],[have_libsystemd=yes],[have_libsystemd=no])
PKG_CHECK_MODULES(LIBSYSTEMD_LOGIN, [libsystemd-login >= 183],[have_libsystemd_login=yes],[have_libsystemd_login=no])
AC_ARG_WITH(suspend-resume,
            AS_HELP_STRING([--with-suspend-resume=no|systemd],
                           [Enable suspend/resume support [[default=auto]]]),,
            [with_suspend_resume=auto])

if test "x$with_suspend_resume" = "xauto"; then
    if test "x$have_libsystemd" = "xyes" || test "x$have_libsystemd_login" = "xyes"; then
        with_suspend_resume=systemd
    else
        with_suspend_resume=no
    fi
fi

case $with_suspend_resume in
    no)
        AC_DEFINE(WITH_SUSPEND_RESUME, 0, [Define if you have suspend-resume support])
        ;;
    systemd)
        if test "x$have_libsystemd" = "xno" && test "x$have_libsystemd_login" = "xno"; then
		    AC_MSG_ERROR(libsystemd or libsystemd-login development headers are required)
	    fi
        AC_DEFINE(WITH_SUSPEND_RESUME, 1, [Define if you have suspend-resume support])
        ;;
    *)
        AC_MSG_ERROR([Wrong value for --with-suspend-resume: $with_suspend_resume])
        ;;
esac

AM_CONDITIONAL(SUSPEND_RESUME_SYSTEMD, test "x$with_suspend_resume" = "xsystemd")

dnl-----------------------------------------------------------------------------
dnl PolicyKit
dnl

PKG_CHECK_MODULES(POLKIT, [polkit-gobject-1 >= 0.97], [have_polkit=yes],[have_polkit=no])
AC_ARG_WITH(polkit,
            AS_HELP_STRING([--with-polkit=(strict|permissive|no)],
                           [Enable PolicyKit support [[default=auto]]]),,
            [with_polkit=auto])

if test "x$with_polkit" = "xauto"; then
	if test "x$have_polkit" = "xno"; then
        with_polkit="no"
    else
        with_polkit="strict"
    fi
elif test "x$with_polkit" = "xyes"; then
    with_polkit=strict
fi

if test "x$with_polkit" = "xno"; then
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

AM_CONDITIONAL(WITH_POLKIT, [test "x$with_polkit" != "xno"])

dnl-----------------------------------------------------------------------------
dnl MBIM support (enabled by default)
dnl

LIBMBIM_VERSION=1.14.0

AC_ARG_WITH(mbim, AS_HELP_STRING([--without-mbim], [Build without MBIM support]), [], [with_mbim=yes])
AM_CONDITIONAL(WITH_MBIM, test "x$with_mbim" = "xyes")
case $with_mbim in
    yes)
        PKG_CHECK_MODULES(MBIM, [mbim-glib >= $LIBMBIM_VERSION], [have_mbim=yes],[have_mbim=no])
        if test "x$have_mbim" = "xno"; then
            AC_MSG_ERROR([Couldn't find libmbim-glib >= $LIBMBIM_VERSION. Install it, or otherwise configure using --without-mbim to disable MBIM support.])
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

dnl-----------------------------------------------------------------------------
dnl QMI support (enabled by default)
dnl

LIBQMI_VERSION=1.16.0

AC_ARG_WITH(qmi, AS_HELP_STRING([--without-qmi], [Build without QMI support]), [], [with_qmi=yes])
AM_CONDITIONAL(WITH_QMI, test "x$with_qmi" = "xyes")
case $with_qmi in
    yes)
        PKG_CHECK_MODULES(QMI, [qmi-glib >= $LIBQMI_VERSION], [have_qmi=yes],[have_qmi=no])
        if test "x$have_qmi" = "xno"; then
            AC_MSG_ERROR([Couldn't find libqmi-glib >= $LIBQMI_VERSION. Install it, or otherwise configure using --without-qmi to disable QMI support.])
        else
            AC_DEFINE(WITH_QMI, 1, [Define if you want QMI support])
            AC_SUBST(QMI_CFLAGS)
            AC_SUBST(QMI_LIBS)
        fi
        ;;
    *)
        with_qmi=no
        ;;
esac

NM_COMPILER_WARNINGS

dnl-----------------------------------------------------------------------------
dnl Distribution version string
dnl
AC_ARG_WITH(dist-version, AS_HELP_STRING([--with-dist-version=<mm-dist-version>], [Define the custom version (like distribution package name and revision)]), ac_distver=$withval, ac_distver="")
if ! test x"$ac_distver" = x""; then
  AC_DEFINE_UNQUOTED(MM_DIST_VERSION, "$ac_distver", [Define the distribution version string])
fi

dnl-----------------------------------------------------------------------------
dnl Protocol libs
dnl

AM_CONDITIONAL(QCDM_STANDALONE, test "yes" = "no")
AM_CONDITIONAL(WMC_STANDALONE, test "yes" = "no")

dnl-----------------------------------------------------------------------------
dnl Protocol libs
dnl

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
examples/sms-python/Makefile
])
AC_OUTPUT

echo "
    ModemManager $VERSION
    ==============================================

    Build:
      compiler:                ${CC}
      cflags:                  ${CFLAGS}
      ldflags:                 ${LDFLAGS}
      maintainer mode:         ${USE_MAINTAINER_MODE}

    System paths:
      prefix:                  ${prefix}
      D-Bus system directory:  ${DBUS_SYS_DIR}
      udev base directory:     ${UDEV_BASE_DIR}
      systemd unit directory:  ${with_systemdsystemunitdir}

    Features:
      policykit support:       ${with_polkit}
      mbim support:            ${with_mbim}
      qmi support:             ${with_qmi}
      suspend/resume support:  ${with_suspend_resume}

    Miscellaneous:
      gobject introspection:   ${found_introspection}
      vala bindings:           ${enable_vala}
      documentation:           ${enable_gtk_doc}
      code coverage:           ${CODE_COVERAGE_ENABLED}"
if test "x${CODE_COVERAGE_ENABLED}" = "xyes"; then
   echo "      code coverage cflags:    ${CODE_COVERAGE_CFLAGS}"
   echo "      code coverage ldflags:   ${CODE_COVERAGE_LDFLAGS}"
fi
echo ""
