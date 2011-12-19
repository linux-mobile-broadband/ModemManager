
lib_LTLIBRARIES = libmm-glib.la

libmm_glib_la_CPPFLAGS = \
	$(LIBMM_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include \
	-I${top_srcdir}/libmm-common \
	-I${top_builddir}/libmm-common

libmm_glib_la_SOURCES = \
	libmm-glib.h \
	mm-helpers.h \
	mm-manager.h \
	mm-manager.c \
	mm-object.h \
	mm-object.c \
	mm-modem.h \
	mm-modem.c \
	mm-modem-3gpp.h \
	mm-modem-3gpp.c \
	mm-sim.h \
	mm-sim.c \
	mm-bearer.h \
	mm-bearer.c

libmm_glib_la_LIBADD = \
	$(top_builddir)/libmm-common/libmm-common.la \
	$(LIBMM_LIBS)

includedir = @includedir@/libmm-glib
include_HEADERS = \
	${top_builddir}/libmm-common/mm-enums-types.h \
	${top_builddir}/libmm-common/mm-errors-types.h \
	${top_builddir}/libmm-common/mm-gdbus-manager.h \
	${top_builddir}/libmm-common/mm-gdbus-sim.h \
	${top_builddir}/libmm-common/mm-gdbus-modem.h \
	${top_builddir}/libmm-common/mm-gdbus-sms.h \
	${top_builddir}/libmm-common/mm-gdbus-bearer.h \
	libmm-glib.h \
	mm-manager.h \
	mm-modem.h
