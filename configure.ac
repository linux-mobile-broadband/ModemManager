AC_PREREQ([2.63])

dnl-----------------------------------------------------------------------------
dnl Package and library versioning support
dnl

m4_define([mm_major_version], [1])
m4_define([mm_minor_version], [14])
m4_define([mm_micro_version], [12])
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
m4_define([mm_glib_lt_current],  [6])
m4_define([mm_glib_lt_revision], [0])
m4_define([mm_glib_lt_age],      [6])

dnl-----------------------------------------------------------------------------
dnl autoconf, automake, libtool initialization
dnl
AC_INIT([ModemManager],
        [mm_version],
        [https://gitlab.freedesktop.org/mobile-broadband/ModemManager/issues],
        [ModemManager])
AM_INIT_AUTOMAKE([1.11.2 subdir-objects tar-ustar no-dist-gzip dist-xz -Wno-portability])
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
AC_PROG_MKDIR_P

dnl Initialize libtool
LT_PREREQ([2.2])
LT_INIT([disable-static])

dnl-----------------------------------------------------------------------------
dnl Compiler warnings
dnl

dnl Make sure autoconf-archive is available
m4_pattern_forbid([^AX_(COMPILER_FLAGS|COMPILER_FLAGS_(CFLAGS|GIR|LDFLAGS))\b],
  [Unexpanded AX_ macro found. Please install GNU autoconf-archive])

dnl Detect git or release builds
AX_IS_RELEASE([git-directory])

dnl Function type cast disabled: used throughout the code especially to
dnl cast GAsyncReadyCallbacks with the real object type instead of GObject
DISABLED_WARNINGS="${DISABLED_WARNINGS} -Wno-cast-function-type"

dnl All message protocol structs are packed, never complain about it
DISABLED_WARNINGS="${DISABLED_WARNINGS} -Wno-packed"

dnl Setup compiler checks
AX_COMPILER_FLAGS()
AX_COMPILER_FLAGS_CFLAGS(,,,[${DISABLED_WARNINGS}])

dnl Specify gnu89 mode
if test "$GCC" = "yes"; then
   CFLAGS="$CFLAGS -std=gnu89"
fi

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

AM_GNU_GETTEXT([external])
AM_GNU_GETTEXT_VERSION([0.19.8])

GETTEXT_PACKAGE=ModemManager
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE,"$GETTEXT_PACKAGE", [Gettext package])

dnl-----------------------------------------------------------------------------
dnl Build dependencies
dnl

# NOTE! If using glib < 2.55.1, make sure you have backported the following patch to avoid
# the daemon leaking a lot: https://gitlab.gnome.org/GNOME/glib/commit/37d9b0c6995f97ea0f744650aaf5179081f878ba
GLIB_MIN_VERSION=2.48.0
GLIB_BUILD_SYMBOLS="-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_48 -DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_48 -DGLIB_DISABLE_DEPRECATION_WARNINGS"

PKG_CHECK_MODULES(MM,
                  glib-2.0 >= $GLIB_MIN_VERSION
                  gmodule-2.0
                  gobject-2.0
                  gio-2.0
                  gio-unix-2.0)
MM_CFLAGS="$MM_CFLAGS $GLIB_BUILD_SYMBOLS"
AC_SUBST(MM_CFLAGS)
AC_SUBST(MM_LIBS)

PKG_CHECK_MODULES(LIBMM_GLIB,
                  glib-2.0 >= $GLIB_MIN_VERSION
                  gobject-2.0
                  gio-2.0
                  gio-unix-2.0)
LIBMM_GLIB_CFLAGS="$LIBMM_GLIB_CFLAGS $GLIB_BUILD_SYMBOLS"
AC_SUBST(LIBMM_GLIB_CFLAGS)
AC_SUBST(LIBMM_GLIB_LIBS)

PKG_CHECK_MODULES(MMCLI,
                  glib-2.0 >= $GLIB_MIN_VERSION
                  gobject-2.0
                  gio-2.0)
MMCLI_CFLAGS="$MMCLI_CFLAGS $GLIB_BUILD_SYMBOLS"
AC_SUBST(MMCLI_CFLAGS)
AC_SUBST(MMCLI_LIBS)

dnl Some required utilities
GLIB_MKENUMS=`$PKG_CONFIG --variable=glib_mkenums glib-2.0`
AC_SUBST(GLIB_MKENUMS)

GDBUS_CODEGEN=`$PKG_CONFIG --variable=gdbus_codegen gio-2.0`
AC_SUBST(GDBUS_CODEGEN)

dnl xsltproc required in git builds only
AC_CHECK_PROG(XSLTPROC_CHECK,xsltproc,yes)
if test "x$ax_is_release" != "xyes" -a "x$XSLTPROC_CHECK" != "xyes"; then
   AC_MSG_ERROR([Please install xsltproc before configuring.])
fi

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
dnl udev support (enabled by default)
dnl

GUDEV_VERSION=147

AC_ARG_WITH(udev, AS_HELP_STRING([--without-udev], [Build without udev support]), [], [with_udev=yes])
AM_CONDITIONAL(WITH_UDEV, test "x$with_udev" = "xyes")
case $with_udev in
    yes)
        PKG_CHECK_MODULES(GUDEV, [gudev-1.0 >= $GUDEV_VERSION], [have_gudev=yes],[have_gudev=no])
        if test "x$have_gudev" = "xno"; then
            AC_MSG_ERROR([Couldn't find gudev >= $GUDEV_VERSION. Install it, or otherwise configure using --without-udev to disable udev support.])
        else
            AC_DEFINE(WITH_UDEV, 1, [Define if you want udev support])
            AC_SUBST(GUDEV_CFLAGS)
            AC_SUBST(GUDEV_LIBS)
        fi
        ;;
    *)
        with_udev=no
        ;;
esac

dnl-----------------------------------------------------------------------------
dnl Suspend/resume support
dnl

PKG_CHECK_MODULES(LIBSYSTEMD, [libsystemd >= 209],[have_libsystemd=yes],[have_libsystemd=no])
PKG_CHECK_MODULES(LIBSYSTEMD_LOGIN, [libsystemd-login >= 183],[have_libsystemd_login=yes],[have_libsystemd_login=no])
AC_ARG_WITH(systemd-suspend-resume,
            AS_HELP_STRING([--with-systemd-suspend-resume=no|yes],
                           [Enable systemd suspend/resume support [[default=auto]]]),,
            [with_systemd_suspend_resume=auto])

if test "x$with_systemd_suspend_resume" = "xauto"; then
    if test "x$have_libsystemd" = "xyes" || test "x$have_libsystemd_login" = "xyes"; then
        with_systemd_suspend_resume=yes
    else
        with_systemd_suspend_resume=no
    fi
fi

case $with_systemd_suspend_resume in
    yes)
        if test "x$have_libsystemd" = "xno" && test "x$have_libsystemd_login" = "xno"; then
		    AC_MSG_ERROR(libsystemd or libsystemd-login development headers are required)
	    fi
        AC_DEFINE(WITH_SYSTEMD_SUSPEND_RESUME, 1, [Define if you have systemd suspend-resume support])
        ;;
    *)
        with_systemd_suspend_resume=no
        ;;
esac

AM_CONDITIONAL(WITH_SYSTEMD_SUSPEND_RESUME, test "x$with_systemd_suspend_resume" = "xyes")

dnl-----------------------------------------------------------------------------
dnl systemd journal support
dnl

AC_ARG_WITH(systemd-journal,
            AS_HELP_STRING([--with-systemd-journal=no|yes|auto],
                           [Enable systemd journal support [[default=auto]]]),,
            [with_systemd_journal=auto])

if test "x$with_systemd_journal" = "xauto"; then
    if test "x$have_libsystemd" = "xyes"; then
        with_systemd_journal=yes
    else
        with_systemd_journal=no
    fi
fi

case $with_systemd_journal in
    yes)
        if test "x$have_libsystemd" = "xno"; then
            AC_MSG_ERROR(libsystemd development headers are required)
        fi
        AC_DEFINE(WITH_SYSTEMD_JOURNAL, 1, [Define if you want systemd journal support])
        ;;
    *)
        with_systemd_journal=no
        ;;
esac

AM_CONDITIONAL(WITH_SYSTEMD_JOURNAL, test "x$with_systemd_journal" = "xyes")

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

MM_POLKIT_SERVICE=""
if test "x$with_polkit" != "xno"; then
	if test "x$have_polkit" = "xno"; then
		AC_MSG_ERROR(PolicyKit development headers are required)
	fi

    case "x$with_polkit" in
        "xpermissive")
            MM_DEFAULT_USER_POLICY="yes"
            MM_POLKIT_SERVICE="polkit.service"
            ;;
        "xstrict")
            MM_DEFAULT_USER_POLICY="auth_self_keep"
            MM_POLKIT_SERVICE="polkit.service"
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

AC_SUBST(MM_POLKIT_SERVICE)
AM_CONDITIONAL(WITH_POLKIT, [test "x$with_polkit" != "xno"])

dnl-----------------------------------------------------------------------------
dnl AT command via DBus support (disabled by default unless running in --debug)
dnl
dnl It is suggested that this option is only enabled in custom built systems and
dnl only if truly required.
dnl

AC_ARG_WITH(at_command_via_dbus,
            AS_HELP_STRING([--with-at-command-via-dbus],
                           [Build with Modem.Command() interface enabled always]),
            [],
            [with_at_command_via_dbus=no])

if test "x$with_at_command_via_dbus" = "xyes"; then
    AC_DEFINE(WITH_AT_COMMAND_VIA_DBUS, 1, [Define if you want to enable AT commands via DBus])
fi

dnl-----------------------------------------------------------------------------
dnl MBIM support (enabled by default)
dnl

LIBMBIM_VERSION=1.24.0

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

LIBQMI_VERSION=1.26.0

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

dnl-----------------------------------------------------------------------------
dnl Plugins
dnl
dnl By default all plugins are built and installed. If the user wants to build only
dnl some specific plugins, this can be done by disabling all first and then enabling
dnl only the ones required, e.g.:
dnl   $ ./configure               \
dnl       --disable-all-plugins   \
dnl       --enable-plugin-generic \
dnl       --enable-plugin-zte ...
dnl
dnl If the user wants all plugins except for some specific ones, those can be
dnl explicitly disabled, e.g.:
dnl   $ ./configure \
dnl       --disable-plugin-generic \
dnl       --disable-plugin-zte ...
dnl

MM_ENABLE_ALL_PLUGINS

MM_ENABLE_PLUGIN([generic])
MM_ENABLE_PLUGIN([altair-lte])
MM_ENABLE_PLUGIN([anydata])
MM_ENABLE_PLUGIN([broadmobi])
MM_ENABLE_PLUGIN([cinterion])
MM_ENABLE_PLUGIN([dell],
                 [with_shared_sierra,
                  with_shared_novatel,
                  with_shared_xmm,
                  with_shared_telit,
                  with_shared_foxconn])
MM_ENABLE_PLUGIN([dlink])
MM_ENABLE_PLUGIN([fibocom],
                 [with_shared_xmm])
MM_ENABLE_PLUGIN([foxconn],
                 [with_shared_foxconn])
MM_ENABLE_PLUGIN([haier])
MM_ENABLE_PLUGIN([huawei])
MM_ENABLE_PLUGIN([iridium])
MM_ENABLE_PLUGIN([linktop])
MM_ENABLE_PLUGIN([longcheer])
MM_ENABLE_PLUGIN([mbm])
MM_ENABLE_PLUGIN([motorola])
MM_ENABLE_PLUGIN([mtk])
MM_ENABLE_PLUGIN([nokia])
MM_ENABLE_PLUGIN([nokia-icera],
                 [with_shared_icera])
MM_ENABLE_PLUGIN([novatel],
                 [with_shared_novatel])
MM_ENABLE_PLUGIN([novatel-lte])
MM_ENABLE_PLUGIN([option],
                 [with_shared_option])
MM_ENABLE_PLUGIN([option-hso],
                 [with_shared_option])
MM_ENABLE_PLUGIN([pantech])
MM_ENABLE_PLUGIN([quectel])
MM_ENABLE_PLUGIN([samsung],
                 [with_shared_icera])
MM_ENABLE_PLUGIN([sierra-legacy],
                 [with_shared_icera,
                  with_shared_sierra])
MM_ENABLE_PLUGIN([sierra])
MM_ENABLE_PLUGIN([simtech])
MM_ENABLE_PLUGIN([telit],
                 [with_shared_telit])
MM_ENABLE_PLUGIN([thuraya])
MM_ENABLE_PLUGIN([tplink])
MM_ENABLE_PLUGIN([ublox])
MM_ENABLE_PLUGIN([via])
MM_ENABLE_PLUGIN([wavecom])
MM_ENABLE_PLUGIN([x22x])
MM_ENABLE_PLUGIN([zte],
                 [with_shared_icera])

MM_BUILD_SHARED([icera])
MM_BUILD_SHARED([sierra])
MM_BUILD_SHARED([option])
MM_BUILD_SHARED([novatel])
MM_BUILD_SHARED([xmm])
MM_BUILD_SHARED([telit])
MM_BUILD_SHARED([foxconn])

dnl-----------------------------------------------------------------------------
dnl Output
dnl

AC_CONFIG_FILES([
Makefile
data/Makefile
data/ModemManager.pc
data/mm-glib.pc
data/tests/Makefile
data/tests/org.freedesktop.ModemManager1.service
include/Makefile
include/ModemManager-version.h
build-aux/Makefile
libqcdm/Makefile
libqcdm/src/Makefile
libqcdm/tests/Makefile
src/Makefile
src/tests/Makefile
plugins/Makefile
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
examples/network-scan-python/Makefile
])
AC_OUTPUT

echo "
    ModemManager $VERSION
    ==============================================

    Build:
      compiler:                ${CC}
      cflags:                  ${CFLAGS}
      ldflags:                 ${LDFLAGS}
      warn cflags:             ${WARN_CFLAGS}
      warn ldflags:            ${WARN_LDFLAGS}
      maintainer mode:         ${USE_MAINTAINER_MODE}
      release:                 ${ax_is_release}

    System paths:
      prefix:                  ${prefix}
      D-Bus system directory:  ${DBUS_SYS_DIR}
      udev base directory:     ${UDEV_BASE_DIR}
      systemd unit directory:  ${with_systemdsystemunitdir}

    Features:
      udev:                    ${with_udev}
      policykit:               ${with_polkit}
      mbim:                    ${with_mbim}
      qmi:                     ${with_qmi}
      systemd suspend/resume:  ${with_systemd_suspend_resume}
      systemd journal:         ${with_systemd_journal}
      at command via dbus:     ${with_at_command_via_dbus}

    Shared utils:
      icera:                   ${with_shared_icera}
      sierra:                  ${with_shared_sierra}
      option:                  ${with_shared_option}
      novatel:                 ${with_shared_novatel}
      xmm:                     ${with_shared_xmm}
      telit:                   ${with_shared_telit}
      foxconn:                 ${with_shared_foxconn}

    Plugins:
      generic:                 ${enable_plugin_generic}
      altair lte:              ${enable_plugin_altair_lte}
      anydata:                 ${enable_plugin_anydata}
      broadmobi:               ${enable_plugin_broadmobi}
      cinterion:               ${enable_plugin_cinterion}
      dell:                    ${enable_plugin_dell}
      dlink:                   ${enable_plugin_dlink}
      fibocom:                 ${enable_plugin_fibocom}
      foxconn:                 ${enable_plugin_foxconn}
      haier:                   ${enable_plugin_haier}
      huawei:                  ${enable_plugin_huawei}
      iridium:                 ${enable_plugin_iridium}
      linktop:                 ${enable_plugin_linktop}
      longcheer:               ${enable_plugin_longcheer}
      mbm:                     ${enable_plugin_mbm}
      motorola:                ${enable_plugin_motorola}
      mtk:                     ${enable_plugin_mtk}
      nokia:                   ${enable_plugin_nokia}
      nokia icera:             ${enable_plugin_nokia_icera}
      novatel:                 ${enable_plugin_novatel}
      novatel lte:             ${enable_plugin_novatel_lte}
      option:                  ${enable_plugin_option}
      option hso:              ${enable_plugin_option_hso}
      pantech:                 ${enable_plugin_pantech}
      quectel:                 ${enable_plugin_quectel}
      samsung:                 ${enable_plugin_samsung}
      sierra legacy:           ${enable_plugin_sierra_legacy}
      sierra:                  ${enable_plugin_sierra}
      simtech:                 ${enable_plugin_simtech}
      telit:                   ${enable_plugin_telit}
      thuraya:                 ${enable_plugin_thuraya}
      tplink:                  ${enable_plugin_tplink}
      ublox:                   ${enable_plugin_ublox}
      via:                     ${enable_plugin_via}
      wavecom:                 ${enable_plugin_wavecom}
      x22x:                    ${enable_plugin_x22x}
      zte:                     ${enable_plugin_zte}

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
