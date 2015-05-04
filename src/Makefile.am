SUBDIRS=. tests

udevrulesdir = $(UDEV_BASE_DIR)/rules.d
udevrules_DATA = \
	77-mm-usb-device-blacklist.rules \
	77-mm-pcmcia-device-blacklist.rules \
	77-mm-platform-serial-whitelist.rules \
	77-mm-usb-serial-adapters-greylist.rules \
	80-mm-candidate.rules

noinst_LTLIBRARIES = libmodem-helpers.la libport.la

libmodem_helpers_la_CPPFLAGS = \
	$(MM_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include \
	-I$(top_srcdir)/libmm-glib \
	-I${top_srcdir}/libmm-glib/generated \
	-I${top_builddir}/libmm-glib/generated

libmodem_helpers_la_LIBADD = \
	$(top_builddir)/libmm-glib/libmm-glib.la

libmodem_helpers_la_SOURCES = \
	mm-error-helpers.c \
	mm-error-helpers.h \
	mm-modem-helpers.c \
	mm-modem-helpers.h \
	mm-charsets.c \
	mm-charsets.h \
	mm-sms-part.h \
	mm-sms-part.c \
	mm-sms-part-3gpp.h \
	mm-sms-part-3gpp.c \
	mm-sms-part-cdma.h \
	mm-sms-part-cdma.c

# Additional QMI support in libmodem-helpers
if WITH_QMI
libmodem_helpers_la_SOURCES += \
	mm-modem-helpers-qmi.c \
	mm-modem-helpers-qmi.h
libmodem_helpers_la_CPPFLAGS += $(QMI_CFLAGS)
endif

# Additional MBIM support in libmodem-helpers
if WITH_MBIM
libmodem_helpers_la_SOURCES += \
	mm-modem-helpers-mbim.c \
	mm-modem-helpers-mbim.h
libmodem_helpers_la_CPPFLAGS += $(MBIM_CFLAGS)
endif

# libport specific enum types
PORT_ENUMS = \
	$(srcdir)/mm-port.h \
	$(srcdir)/mm-port-serial-at.h

mm-port-enums-types.h: Makefile.am $(PORT_ENUMS) $(top_srcdir)/build-aux/mm-enums-template.h
	$(AM_V_GEN) $(GLIB_MKENUMS) \
		--fhead "#include \"mm-port.h\"\n#include \"mm-port-serial-at.h\"\n#ifndef __MM_PORT_ENUMS_TYPES_H__\n#define __MM_PORT_ENUMS_TYPES_H__\n" \
		--template $(top_srcdir)/build-aux/mm-enums-template.h \
		--ftail "#endif /* __MM_PORT_ENUMS_TYPES_H__ */\n" \
		$(PORT_ENUMS) > $@

mm-port-enums-types.c: Makefile.am $(top_srcdir)/build-aux/mm-enums-template.c mm-port-enums-types.h
	$(AM_V_GEN) $(GLIB_MKENUMS) \
		--fhead "#include \"mm-port-enums-types.h\"" \
		--template $(top_srcdir)/build-aux/mm-enums-template.c \
		$(PORT_ENUMS) > $@

libport_la_CPPFLAGS = \
	$(MM_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include \
	-I$(top_srcdir)/libmm-glib \
	-I${top_srcdir}/libmm-glib/generated \
	-I${top_builddir}/libmm-glib/generated

nodist_libport_la_SOURCES = \
	mm-port-enums-types.h \
	mm-port-enums-types.c

libport_la_SOURCES = \
	mm-port.c \
	mm-port.h \
	mm-port-serial.c \
	mm-port-serial.h \
	mm-port-serial-at.c \
	mm-port-serial-at.h \
	mm-port-serial-qcdm.c \
	mm-port-serial-qcdm.h \
	mm-port-serial-gps.c \
	mm-port-serial-gps.h

# Additional QMI support in libserial
if WITH_QMI
libport_la_SOURCES += \
	mm-port-qmi.c \
	mm-port-qmi.h
libport_la_CPPFLAGS += $(QMI_CFLAGS)
endif

# Additional MBIM support in libserial
if WITH_MBIM
libport_la_SOURCES += \
	mm-port-mbim.c \
	mm-port-mbim.h
libport_la_CPPFLAGS += $(MBIM_CFLAGS)
endif

# Daemon specific enum types
DAEMON_ENUMS = \
	$(srcdir)/mm-base-bearer.h \
	$(srcdir)/mm-port-probe.h

mm-daemon-enums-types.h: Makefile.am $(DAEMON_ENUMS) $(top_srcdir)/build-aux/mm-enums-template.h
	$(AM_V_GEN) $(GLIB_MKENUMS) \
		--fhead "#include \"mm-base-bearer.h\"\n#include \"mm-port-probe.h\"\n#ifndef __MM_DAEMON_ENUMS_TYPES_H__\n#define __MM_DAEMON_ENUMS_TYPES_H__\n" \
		--template $(top_srcdir)/build-aux/mm-enums-template.h \
		--ftail "#endif /* __MM_DAEMON_ENUMS_TYPES_H__ */\n" \
		$(DAEMON_ENUMS) > $@

mm-daemon-enums-types.c: Makefile.am $(top_srcdir)/build-aux/mm-enums-template.c mm-daemon-enums-types.h
	$(AM_V_GEN) $(GLIB_MKENUMS) \
		--fhead "#include \"mm-daemon-enums-types.h\"" \
		--template $(top_srcdir)/build-aux/mm-enums-template.c \
		$(DAEMON_ENUMS) > $@

sbin_PROGRAMS = ModemManager

ModemManager_CPPFLAGS = \
	$(MM_CFLAGS) \
	$(GUDEV_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include \
	-I$(top_srcdir)/libmm-glib \
	-I$(top_builddir)/libmm-glib \
	-I${top_srcdir}/libmm-glib/generated \
	-I${top_builddir}/libmm-glib/generated \
	-I${top_srcdir}/libmm-glib/generated/tests \
	-I${top_builddir}/libmm-glib/generated/tests \
	-DPLUGINDIR=\"$(pkglibdir)\"

ModemManager_LDADD = \
	$(MM_LIBS) \
	$(GUDEV_LIBS) \
	$(builddir)/libmodem-helpers.la \
	$(builddir)/libport.la \
	$(top_builddir)/libqcdm/src/libqcdm.la \
	$(top_builddir)/libmm-glib/generated/tests/libmm-test-generated.la

nodist_ModemManager_SOURCES = \
	mm-daemon-enums-types.h \
	mm-daemon-enums-types.c

ModemManager_SOURCES = \
	main.c \
	mm-context.h \
	mm-context.c \
	mm-log.c \
	mm-log.h \
	mm-private-boxed-types.h \
	mm-private-boxed-types.c \
	mm-auth.h \
	mm-auth.c \
	mm-auth-provider.h \
	mm-auth-provider.c \
	mm-base-manager.c \
	mm-base-manager.h \
	mm-device.c \
	mm-device.h \
	mm-plugin-manager.c \
	mm-plugin-manager.h \
	mm-base-sim.h \
	mm-base-sim.c \
	mm-base-bearer.h \
	mm-base-bearer.c \
	mm-broadband-bearer.h \
	mm-broadband-bearer.c \
	mm-bearer-list.h \
	mm-bearer-list.c \
	mm-base-modem-at.h \
	mm-base-modem-at.c \
	mm-base-modem.h \
	mm-base-modem.c \
	mm-base-sms.h \
	mm-base-sms.c \
	mm-sms-list.h \
	mm-sms-list.c \
	mm-iface-modem.h \
	mm-iface-modem.c \
	mm-iface-modem-3gpp.h \
	mm-iface-modem-3gpp.c \
	mm-iface-modem-3gpp-ussd.h \
	mm-iface-modem-3gpp-ussd.c \
	mm-iface-modem-cdma.h \
	mm-iface-modem-cdma.c \
	mm-iface-modem-simple.h \
	mm-iface-modem-simple.c \
	mm-iface-modem-location.h \
	mm-iface-modem-location.c \
	mm-iface-modem-messaging.h \
	mm-iface-modem-messaging.c \
	mm-iface-modem-time.h \
	mm-iface-modem-time.c \
	mm-iface-modem-firmware.h \
	mm-iface-modem-firmware.c \
	mm-iface-modem-signal.h \
	mm-iface-modem-signal.c \
	mm-iface-modem-oma.h \
	mm-iface-modem-oma.c \
	mm-broadband-modem.h \
	mm-broadband-modem.c \
	mm-serial-parsers.c \
	mm-serial-parsers.h \
	mm-port-probe.h \
	mm-port-probe.c \
	mm-port-probe-at.h \
	mm-port-probe-at.c \
	mm-plugin.c \
	mm-plugin.h

# Additional Polkit support
if WITH_POLKIT
ModemManager_SOURCES += \
	mm-auth-provider-polkit.c \
	mm-auth-provider-polkit.h
ModemManager_LDADD += $(POLKIT_LIBS)
ModemManager_CPPFLAGS += $(POLKIT_CFLAGS)
endif

# Additional QMI support in ModemManager
if WITH_QMI
ModemManager_SOURCES += \
	mm-sms-qmi.h \
	mm-sms-qmi.c \
	mm-sim-qmi.h \
	mm-sim-qmi.c \
	mm-bearer-qmi.h \
	mm-bearer-qmi.c \
	mm-broadband-modem-qmi.h \
	mm-broadband-modem-qmi.c
ModemManager_CPPFLAGS += $(QMI_CFLAGS)
ModemManager_LDADD += $(QMI_LIBS)
endif

# Additional MBIM support in ModemManager
if WITH_MBIM
ModemManager_SOURCES += \
	mm-sms-mbim.h \
	mm-sms-mbim.c \
	mm-sim-mbim.h \
	mm-sim-mbim.c \
	mm-bearer-mbim.h \
	mm-bearer-mbim.c \
	mm-broadband-modem-mbim.h \
	mm-broadband-modem-mbim.c
ModemManager_CPPFLAGS += $(MBIM_CFLAGS)
ModemManager_LDADD += $(MBIM_LIBS)
endif

EXTRA_DIST = \
	$(udevrules_DATA)

# Request to build enum types before anything else
BUILT_SOURCES = \
	mm-daemon-enums-types.h \
	mm-daemon-enums-types.c \
	mm-port-enums-types.h \
	mm-port-enums-types.c

CLEANFILES = $(BUILT_SOURCES)
