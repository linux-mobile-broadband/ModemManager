SUBDIRS = generated . tests

lib_LTLIBRARIES = libmm-glib.la

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
	mm-modem-firmware.h \
	mm-modem-firmware.c \
	mm-sim.h \
	mm-sim.c \
	mm-sms.h \
	mm-sms.c \
	mm-modem-messaging.h \
	mm-modem-messaging.c \
	mm-bearer.h \
	mm-bearer.c \
	mm-common-helpers.h \
	mm-common-helpers.c \
	mm-simple-status.h \
	mm-simple-status.c \
	mm-simple-connect-properties.h \
	mm-simple-connect-properties.c \
	mm-bearer-properties.h \
	mm-bearer-properties.c \
	mm-sms-properties.h \
	mm-sms-properties.c \
	mm-bearer-ip-config.h \
	mm-bearer-ip-config.c \
	mm-location-common.h \
	mm-location-3gpp.h \
	mm-location-3gpp.c \
	mm-location-gps-raw.h \
	mm-location-gps-raw.c \
	mm-location-gps-nmea.h \
	mm-location-gps-nmea.c \
	mm-location-cdma-bs.h \
	mm-location-cdma-bs.c \
	mm-unlock-retries.h \
	mm-unlock-retries.c \
	mm-network-timezone.h \
	mm-network-timezone.c \
	mm-firmware-properties.h \
	mm-firmware-properties.c

libmm_glib_la_CPPFLAGS = \
	$(LIBMM_GLIB_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include \
	-I${top_srcdir}/libmm-glib/generated \
	-I${top_builddir}/libmm-glib/generated \
	-DLIBMM_GLIB_COMPILATION

libmm_glib_la_LIBADD = \
	${top_builddir}/libmm-glib/generated/libmm-generated.la \
	$(LIBMM_GLIB_LIBS)

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
	mm-modem-firmware.h \
	mm-modem-simple.h \
	mm-sim.h \
	mm-sms.h \
	mm-bearer.h \
	mm-simple-status.h \
	mm-simple-connect-properties.h \
	mm-bearer-properties.h \
	mm-sms-properties.h \
	mm-bearer-ip-config.h \
	mm-location-common.h \
	mm-location-3gpp.h \
	mm-location-gps-nmea.h \
	mm-location-gps-raw.h \
	mm-location-cdma-bs.h \
	mm-unlock-retries.h \
	mm-network-timezone.h \
	mm-firmware-properties.h
