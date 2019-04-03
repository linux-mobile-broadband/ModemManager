AC_DEFUN([NM_COMPILER_WARNINGS],
[AC_ARG_ENABLE(more-warnings,
	AS_HELP_STRING([--enable-more-warnings], [Possible values: no/yes/error]),
	set_more_warnings="$enableval",set_more_warnings=error)
AC_MSG_CHECKING(for more warnings)
if test "$GCC" = "yes" -a "$set_more_warnings" != "no"; then
	AC_MSG_RESULT(yes)
	CFLAGS="-Wall -std=gnu89 $CFLAGS"

	for option in -Wmissing-declarations -Wmissing-prototypes \
		      -Wdeclaration-after-statement -Wstrict-prototypes \
		      -Wno-unused-parameter -Wno-sign-compare \
		      -Wno-deprecated-declarations -Wno-unused-function \
		      -Wunused-variable -Wformat-security; do
		SAVE_CFLAGS="$CFLAGS"
		CFLAGS="$CFLAGS $option -Werror"
		AC_MSG_CHECKING([whether gcc understands $option])
		AC_TRY_COMPILE([], [],
			has_option=yes,
			has_option=no,)
		if test $has_option = no; then
			CFLAGS="$SAVE_CFLAGS"
		fi
		AC_MSG_RESULT($has_option)
		unset has_option
		unset SAVE_CFLAGS
	done
	unset option
	if test "x$set_more_warnings" = xerror; then
		CFLAGS="$CFLAGS -Werror"
	fi
else
	AC_MSG_RESULT(no)
fi
])
