AC_PREREQ([2.60])

AC_INIT([ModemManager],[0.7.990],[dcbw@redhat.com],[ModemManager])
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
dnl Documentation
dnl

AC_ARG_WITH(docs, AS_HELP_STRING([--with-docs], [Build ModemManager documentation]))
AM_CONDITIONAL(WITH_DOCS, test "x$with_docs" = "xyes")
case $with_docs in
    yes)
        # Check for dia if we are building gtk_doc
        AC_PATH_PROG(DIA, dia)
        AC_SUBST(DIA)

        if test -z "$DIA"; then
           AC_MSG_ERROR([Could not find Dia tool required to build documentation.])
        fi

        enable_gtk_doc=yes
        ;;
    *)
        with_docs=no
        ;;
esac

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
GLIB_GENMARSHAL=`$PKG_CONFIG --variable=glib_genmarshal glib-2.0`
AC_SUBST(GLIB_GENMARSHAL)
GLIB_MKENUMS=`$PKG_CONFIG --variable=glib_mkenums glib-2.0`
AC_SUBST(GLIB_MKENUMS)

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
AC_ARG_WITH(polkit, AS_HELP_STRING([--with-polkit], [Build with PolicyKit support]))
AM_CONDITIONAL(WITH_POLKIT, test "x$with_polkit" = "xyes")
case $with_polkit in
    yes)
        with_polkit=yes
	PKG_CHECK_MODULES(POLKIT, polkit-gobject-1 >= 0.97)
	AC_DEFINE(WITH_POLKIT, 1, [Define if you want to use PolicyKit])
	AC_SUBST(POLKIT_CFLAGS)
	AC_SUBST(POLKIT_LIBS)

	AM_GLIB_GNU_GETTEXT
        ;;
    *)
        with_polkit=no
        ;;
esac

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

dnl
dnl MBIM support (enabled by default)
dnl
AC_ARG_WITH(mbim, AS_HELP_STRING([--without-mbim], [Build without MBIM support]), [], [with_mbim=yes])
AM_CONDITIONAL(WITH_MBIM, test "x$with_mbim" = "xyes")
case $with_mbim in
    yes)
        PKG_CHECK_MODULES(MBIM, mbim-glib)
        AC_DEFINE(WITH_MBIM, 1, [Define if you want MBIM support])
        AC_SUBST(MBIM_CFLAGS)
        AC_SUBST(MBIM_LIBS)
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
        PKG_CHECK_MODULES(QMI, qmi-glib)
        AC_DEFINE(WITH_QMI, 1, [Define if you want QMI support])
        AC_SUBST(QMI_CFLAGS)
        AC_SUBST(QMI_LIBS)
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
data/org.freedesktop.ModemManager1.policy
include/Makefile
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
libmm-glib/tests/Makefile
cli/Makefile
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

    PPP-enabled tests:       ${have_pppd_headers}
    PolicyKit support:       ${with_polkit}
    Documentation:           ${with_docs}
    MBIM support:            ${with_mbim}
    QMI support:             ${with_qmi}
    Newest QMI commands:     ${with_newest_qmi_commands}
"
