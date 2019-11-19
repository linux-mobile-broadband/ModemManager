SUBDIRS = generated . tests

AM_CFLAGS  = $(CODE_COVERAGE_CFLAGS)
AM_LDFLAGS = $(CODE_COVERAGE_LDFLAGS)

lib_LTLIBRARIES = libmm-glib.la

libmm_glib_la_SOURCES = \
	libmm-glib.h \
	mm-helpers.h \
	mm-helper-types.h \
	mm-helper-types.c \
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
	mm-modem-signal.h \
	mm-modem-signal.c \
	mm-modem-oma.h \
	mm-modem-oma.c \
	mm-sim.h \
	mm-sim.c \
	mm-sms.h \
	mm-sms.c \
	mm-modem-messaging.h \
	mm-modem-messaging.c \
	mm-modem-voice.h \
	mm-modem-voice.c \
	mm-call.h \
	mm-call.c \
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
	mm-call-properties.h \
	mm-call-properties.c \
	mm-bearer-ip-config.h \
	mm-bearer-ip-config.c \
	mm-bearer-stats.h \
	mm-bearer-stats.c \
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
	mm-firmware-properties.c \
	mm-firmware-update-settings.h \
	mm-firmware-update-settings.c \
	mm-cdma-manual-activation-properties.h \
	mm-cdma-manual-activation-properties.c \
	mm-signal.h \
	mm-signal.c \
	mm-kernel-event-properties.h \
	mm-kernel-event-properties.c \
	mm-pco.h \
	mm-pco.c \
	mm-call-audio-format.h \
	mm-call-audio-format.c \
	$(NULL)

libmm_glib_la_CPPFLAGS = \
	$(LIBMM_GLIB_CFLAGS) \
	-I$(srcdir) \
	-I$(top_srcdir) \
	-I$(top_builddir) \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include \
	-I${top_srcdir}/libmm-glib/generated \
	-I${top_builddir}/libmm-glib/generated \
	-DLIBMM_GLIB_COMPILATION \
	$(NULL)

libmm_glib_gla_CFLAGS = \
	$(WARN_CFLAGS) \
	$(NULL)

libmm_glib_la_LIBADD = \
	${top_builddir}/libmm-glib/generated/libmm-generated.la \
	$(NULL)

libmm_glib_la_LDFLAGS = \
	-version-info $(MM_GLIB_LT_CURRENT):$(MM_GLIB_LT_REVISION):$(MM_GLIB_LT_AGE) \
	$(WARN_LDFLAGS) \
	$(LIBMM_GLIB_LIBS) \
	$(NULL)

includedir = @includedir@/libmm-glib
include_HEADERS = \
	libmm-glib.h \
	mm-helper-types.h \
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
	mm-modem-signal.h \
	mm-modem-oma.h \
	mm-modem-simple.h \
	mm-sim.h \
	mm-sms.h \
	mm-modem-voice.h \
	mm-call.h \
	mm-bearer.h \
	mm-simple-status.h \
	mm-simple-connect-properties.h \
	mm-bearer-properties.h \
	mm-sms-properties.h \
	mm-call-properties.h \
	mm-bearer-ip-config.h \
	mm-bearer-stats.h \
	mm-location-common.h \
	mm-location-3gpp.h \
	mm-location-gps-nmea.h \
	mm-location-gps-raw.h \
	mm-location-cdma-bs.h \
	mm-unlock-retries.h \
	mm-network-timezone.h \
	mm-firmware-properties.h \
	mm-firmware-update-settings.h \
	mm-cdma-manual-activation-properties.h \
	mm-signal.h \
	mm-kernel-event-properties.h \
	mm-pco.h \
	mm-call-audio-format.h \
	$(NULL)

CLEANFILES =

# Introspection

if HAVE_INTROSPECTION

GENERATED_H = \
	mm-enums-types.h \
	mm-errors-types.h \
	mm-gdbus-manager.h \
	mm-gdbus-sim.h \
	mm-gdbus-sms.h \
	mm-gdbus-bearer.h \
	mm-gdbus-modem.h \
	$(NULL)

GENERATED_C = \
	mm-enums-types.c \
	mm-errors-types.c \
	mm-errors-quarks.c \
	mm-gdbus-manager.c \
	mm-gdbus-sim.c \
	mm-gdbus-sms.c \
	mm-gdbus-bearer.c \
	mm-gdbus-modem.c \
	$(NULL)

PUBLIC_H = \
	ModemManager-names.h \
	ModemManager-version.h
	ModemManager-enums.h \
	ModemManager-errors.h \
	ModemManager-compat.h \
	ModemManager.h \
	$(NULL)

INTROSPECTION_GIRS = ModemManager-1.0.gir
INTROSPECTION_SCANNER_ARGS = --warn-all
INTROSPECTION_COMPILER_ARGS =

ModemManager-1.0.gir: libmm-glib.la
ModemManager_1_0_gir_INCLUDES = GLib-2.0 GObject-2.0 Gio-2.0
ModemManager_1_0_gir_CFLAGS = $(libmm_glib_la_CPPFLAGS)
ModemManager_1_0_gir_LIBS = libmm-glib.la
ModemManager_1_0_gir_EXPORT_PACKAGES = libmm-glib
ModemManager_1_0_gir_SCANNERFLAGS = \
	$(WARN_SCANNERFLAGS) \
	--c-include "libmm-glib.h" \
	--identifier-prefix=MM \
	--identifier-prefix=Mm \
	--symbol-prefix=mm \
	$(NULL)
ModemManager_1_0_gir_FILES = \
	$(include_HEADERS) \
	$(filter-out %.h,$(libmm_glib_la_SOURCES)) \
	$(filter %.c,$(libmm_glib_la_SOURCES)) \
	$(addprefix generated/,$(GENERATED_H)) \
	$(addprefix generated/,$(GENERATED_C)) \
	$(addprefix ../include/,$(PUBLIC_H)) \
	$(NULL)

girdir = $(datadir)/gir-1.0
nodist_gir_DATA = $(INTROSPECTION_GIRS)

typelibdir = $(libdir)/girepository-1.0
nodist_typelib_DATA = $(INTROSPECTION_GIRS:.gir=.typelib)

CLEANFILES += $(nodist_gir_DATA) $(nodist_typelib_DATA)

endif # HAVE_INTROSPECTION

-include $(INTROSPECTION_MAKEFILE)
