dnl -*- mode: autoconf -*-
dnl Copyright 2019 Aleksander Morgado
dnl
dnl This file is free software; the author(s) gives unlimited
dnl permission to copy and/or distribute it, with or without
dnl modifications, as long as this notice is preserved.
dnl

# serial 1

dnl Usage:
dnl  MM_ENABLE_ALL_PLUGINS
AC_DEFUN([MM_ENABLE_ALL_PLUGINS],
[dnl
AC_ARG_ENABLE(all-plugins,
              AS_HELP_STRING([--enable-all-plugins],
              [Build all plugins [[default=yes]]]),
              [],
              [enable_all_plugins=yes])
])

dnl Usage:
dnl  MM_ENABLE_PLUGIN_FULL([NAME],[DEFAULT],[WITH_SHARED_NAME_1,WITH_SHARED_NAME_2,...])
AC_DEFUN([MM_ENABLE_PLUGIN_FULL],
[dnl
m4_pushdef([var_enable_plugin], patsubst([enable_plugin_$1], -, _))dnl
m4_pushdef([VAR_ENABLE_PLUGIN], patsubst(translit([enable_plugin_$1], [a-z], [A-Z]), -, _))dnl
AC_ARG_ENABLE(plugin-$1,
              AS_HELP_STRING([--enable-plugin-$1], [Build $1 plugin]),
              [],
              [var_enable_plugin=$2])
if test "x$var_enable_plugin" = "xyes"; then
  AC_DEFINE([VAR_ENABLE_PLUGIN], 1, [Define if $1 plugin is enabled])
m4_ifval([$3],[m4_foreach(with_shared,[$3],[dnl
  with_shared="yes"
])])dnl
fi
AM_CONDITIONAL(VAR_ENABLE_PLUGIN, [test "x$var_enable_plugin" = "xyes"])
m4_popdef([VAR_ENABLE_PLUGIN])dnl
m4_popdef([var_enable_plugin])dnl
])

dnl Usage:
dnl  MM_ENABLE_PLUGIN([NAME],[WITH_SHARED_NAME_1,WITH_SHARED_NAME_2,...])
AC_DEFUN([MM_ENABLE_PLUGIN],
[dnl
MM_ENABLE_PLUGIN_FULL([$1],[$enable_all_plugins],[$2])
])

dnl Usage:
dnl  MM_ENABLE_PLUGIN_DISABLED([NAME],[WITH_SHARED_NAME_1,WITH_SHARED_NAME_2,...])
AC_DEFUN([MM_ENABLE_PLUGIN_DISABLED],
[dnl
MM_ENABLE_PLUGIN_FULL([$1],["no"],[$2])
])

dnl Usage:
dnl  MM_BUILD_SHARED([NAME])
AC_DEFUN([MM_BUILD_SHARED],
[dnl
m4_pushdef([with_shared], patsubst([with_shared_$1], -, _))dnl
m4_pushdef([WITH_SHARED], patsubst(translit([with_shared_$1], [a-z], [A-Z]), -, _))dnl
AM_CONDITIONAL(WITH_SHARED, test "x$with_shared" = "xyes")
if test "x$with_shared" = "xyes"; then
  AC_DEFINE([WITH_SHARED], 1, [Define if $1 utils are built])
else
  with_shared="no"
fi
m4_popdef([WITH_SHARED])dnl
m4_popdef([with_shared])dnl
])
