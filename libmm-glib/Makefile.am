
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
	mm-modem-3gpp-ussd.h \
	mm-modem-3gpp-ussd.c \
	mm-modem-cdma.h \
	mm-modem-cdma.c \
	mm-modem-simple.h \
	mm-modem-simple.c \
	mm-modem-location.h \
	mm-modem-location.c \
	mm-modem-time.h \
	mm-modem-time.c \
	mm-sim.h \
	mm-sim.c \
	mm-sms.h \
	mm-sms.c \
	mm-modem-messaging.h \
	mm-modem-messaging.c \
	mm-bearer.h \
	mm-bearer.c

libmm_glib_la_LIBADD = \
	$(top_builddir)/libmm-common/libmm-common.la \
	$(LIBMM_LIBS)

includedir = @includedir@/libmm-glib
include_HEADERS = \
	libmm-glib.h \
	mm-manager.h \
	mm-object.h \
	mm-modem.h \
	mm-modem-3gpp.h \
	mm-modem-3gpp-ussd.h \
	mm-modem-cdma.h \
	mm-modem-messaging.h \
	mm-modem-location.h \
	mm-modem-time.h \
	mm-modem-simple.h \
	mm-sim.h \
	mm-sms.h \
	mm-bearer.h
