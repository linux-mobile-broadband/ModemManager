SUBDIRS=. tests

udevrulesdir = $(UDEV_BASE_DIR)/rules.d
udevrules_DATA = \
	77-mm-usb-device-blacklist.rules \
	77-mm-pcmcia-device-blacklist.rules \
	77-mm-platform-serial-whitelist.rules \
	77-mm-usb-serial-adapters-greylist.rules \
	80-mm-candidate.rules

noinst_LTLIBRARIES = libmodem-helpers.la libserial.la

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
	mm-sms-part.c

# Additional QMI support in libmodem-helpers
if WITH_QMI
libmodem_helpers_la_SOURCES += \
	mm-modem-helpers-qmi.c \
	mm-modem-helpers-qmi.h
libmodem_helpers_la_CPPFLAGS += $(QMI_CFLAGS)
endif

# libserial specific enum types
SERIAL_ENUMS = \
	$(srcdir)/mm-port.h \
	$(srcdir)/mm-at-serial-port.h

mm-serial-enums-types.h: Makefile.am $(SERIAL_ENUMS) $(top_srcdir)/build-aux/mm-enums-template.h
	$(AM_V_GEN) $(GLIB_MKENUMS) \
		--fhead "#include \"mm-port.h\"\n#include \"mm-at-serial-port.h\"\n#ifndef __MM_SERIAL_ENUMS_TYPES_H__\n#define __MM_SERIAL_ENUMS_TYPES_H__\n" \
		--template $(top_srcdir)/build-aux/mm-enums-template.h \
		--ftail "#endif /* __MM_SERIAL_ENUMS_TYPES_H__ */\n" \
		$(SERIAL_ENUMS) > $@

mm-serial-enums-types.c: Makefile.am $(top_srcdir)/build-aux/mm-enums-template.c mm-serial-enums-types.h
	$(AM_V_GEN) $(GLIB_MKENUMS) \
		--fhead "#include \"mm-serial-enums-types.h\"" \
		--template $(top_srcdir)/build-aux/mm-enums-template.c \
		$(SERIAL_ENUMS) > $@

libserial_la_CPPFLAGS = \
	$(MM_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include \
	-I$(top_srcdir)/libmm-glib \
	-I${top_srcdir}/libmm-glib/generated \
	-I${top_builddir}/libmm-glib/generated

nodist_libserial_la_SOURCES = \
	mm-serial-enums-types.h \
	mm-serial-enums-types.c

libserial_la_SOURCES = \
	mm-port.c \
	mm-port.h \
	mm-serial-port.c \
	mm-serial-port.h \
	mm-at-serial-port.c \
	mm-at-serial-port.h \
	mm-qcdm-serial-port.c \
	mm-qcdm-serial-port.h \
	mm-gps-serial-port.c \
	mm-gps-serial-port.h

# Additional QMI support in libserial
if WITH_QMI
libserial_la_SOURCES += \
	mm-qmi-port.c \
	mm-qmi-port.h
libserial_la_CPPFLAGS += $(QMI_CFLAGS)
endif

# Additional MBIM support in libserial
if WITH_MBIM
libserial_la_SOURCES += \
	mm-mbim-port.c \
	mm-mbim-port.h
libserial_la_CPPFLAGS += $(MBIM_CFLAGS)
endif

# Daemon specific enum types
DAEMON_ENUMS = \
	$(srcdir)/mm-bearer.h \
	$(srcdir)/mm-port-probe.h

mm-daemon-enums-types.h: Makefile.am $(DAEMON_ENUMS) $(top_srcdir)/build-aux/mm-enums-template.h
	$(AM_V_GEN) $(GLIB_MKENUMS) \
		--fhead "#include \"mm-bearer.h\"\n#include \"mm-port-probe.h\"\n#ifndef __MM_DAEMON_ENUMS_TYPES_H__\n#define __MM_DAEMON_ENUMS_TYPES_H__\n" \
		--template $(top_srcdir)/build-aux/mm-enums-template.h \
		--ftail "#endif /* __MM_DAEMON_ENUMS_TYPES_H__ */\n" \
		$(DAEMON_ENUMS) > $@

mm-daemon-enums-types.c: Makefile.am $(top_srcdir)/build-aux/mm-enums-template.c mm-daemon-enums-types.h
	$(AM_V_GEN) $(GLIB_MKENUMS) \
		--fhead "#include \"mm-daemon-enums-types.h\"" \
		--template $(top_srcdir)/build-aux/mm-enums-template.c \
		$(DAEMON_ENUMS) > $@

# Marshallers

mm-marshal.h: mm-marshal.list
	$(AM_V_GEN) $(GLIB_GENMARSHAL) $< --prefix=mm_marshal --header > $@

mm-marshal.c: mm-marshal.list mm-marshal.h
	$(AM_V_GEN) echo "#include \"mm-marshal.h\"" > $@ && \
		$(GLIB_GENMARSHAL) $< --prefix=mm_marshal --body >> $@

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
	-DPLUGINDIR=\"$(pkglibdir)\"

ModemManager_LDADD = \
	$(MM_LIBS) \
	$(GUDEV_LIBS) \
	$(builddir)/libmodem-helpers.la \
	$(builddir)/libserial.la \
	$(top_builddir)/libqcdm/src/libqcdm.la

nodist_ModemManager_SOURCES = \
	mm-marshal.h \
	mm-marshal.c \
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
	mm-manager.c \
	mm-manager.h \
	mm-device.c \
	mm-device.h \
	mm-plugin-manager.c \
	mm-plugin-manager.h \
	mm-sim.h \
	mm-sim.c \
	mm-bearer.h \
	mm-bearer.c \
	mm-broadband-bearer.h \
	mm-broadband-bearer.c \
	mm-bearer-list.h \
	mm-bearer-list.c \
	mm-base-modem-at.h \
	mm-base-modem-at.c \
	mm-base-modem.h \
	mm-base-modem.c \
	mm-sms-part.h \
	mm-sms-part.c \
	mm-sms.h \
	mm-sms.c \
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

# Additional dependency rules
mm-bearer.c: mm-daemon-enums-types.h
mm-sms-list.c: mm-marshal.h
mm-sim.c: mm-marshal.h

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
ModemManager_CPPFLAGS += $(MBIM_CFLAGS)
ModemManager_LDADD += $(MBIM_LIBS)
endif

EXTRA_DIST = \
	$(udevrules_DATA) \
	mm-marshal.list

CLEANFILES = \
	mm-marshal.h \
	mm-marshal.c \
	mm-daemon-enums-types.h \
	mm-daemon-enums-types.c \
	mm-serial-enums-types.h \
	mm-serial-enums-types.c
