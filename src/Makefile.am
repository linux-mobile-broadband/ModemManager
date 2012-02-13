SUBDIRS=. tests

udevrulesdir = $(UDEV_BASE_DIR)/rules.d
udevrules_DATA = \
	77-mm-usb-device-blacklist.rules \
	77-mm-pcmcia-device-blacklist.rules \
	77-mm-platform-serial-whitelist.rules \
	80-mm-candidate.rules

EXTRA_DIST = \
	$(udevrules_DATA)

noinst_LTLIBRARIES = libmodem-helpers.la libserial.la

libmodem_helpers_la_CPPFLAGS = \
	$(MM_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include \
	-I$(top_srcdir)/libmm-common \
	-I$(top_builddir)/libmm-common

libmodem_helpers_la_LIBADD = \
	$(top_builddir)/libmm-common/libmm-common.la

libmodem_helpers_la_SOURCES = \
	mm-error-helpers.c \
	mm-error-helpers.h \
	mm-modem-helpers.c \
	mm-modem-helpers.h \
	mm-charsets.c \
	mm-charsets.h \
	mm-utils.c \
	mm-utils.h \
	mm-sms-utils.c \
	mm-sms-utils.h

# libserial specific enum types
SERIAL_ENUMS = mm-port.h

mm-serial-enums-types.h: Makefile.am $(SERIAL_ENUMS) $(top_srcdir)/build-aux/mm-enums-template.h
	$(AM_V_GEN) glib-mkenums \
		--fhead "#include \"mm-port.h\"\n#ifndef __MM_SERIAL_ENUMS_TYPES_H__\n#define __MM_SERIAL_ENUMS_TYPES_H__\n" \
		--template $(top_srcdir)/build-aux/mm-enums-template.h \
		--ftail "#endif /* __MM_SERIAL_ENUMS_TYPES_H__ */\n" \
		$(SERIAL_ENUMS) > $@

mm-serial-enums-types.c: Makefile.am $(top_srcdir)/build-aux/mm-enums-template.c mm-serial-enums-types.h
	$(AM_V_GEN) glib-mkenums \
		--fhead "#include \"mm-serial-enums-types.h\"" \
		--template $(top_srcdir)/build-aux/mm-enums-template.c \
		$(SERIAL_ENUMS) > $@

libserial_la_CPPFLAGS = \
	$(MM_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include \
	-I$(top_srcdir)/libmm-common \
	-I$(top_builddir)/libmm-common

libserial_la_SOURCES = \
	mm-port.c \
	mm-port.h \
	mm-serial-port.c \
	mm-serial-port.h \
	mm-at-serial-port.c \
	mm-at-serial-port.h \
	mm-qcdm-serial-port.c \
	mm-qcdm-serial-port.h \
	mm-serial-enums-types.h \
	mm-serial-enums-types.c

# Daemon specific enum types
DAEMON_ENUMS = mm-bearer.h

mm-daemon-enums-types.h: Makefile.am $(DAEMON_ENUMS) $(top_srcdir)/build-aux/mm-enums-template.h
	$(AM_V_GEN) glib-mkenums \
		--fhead "#include \"mm-bearer.h\"\n#ifndef __MM_DAEMON_ENUMS_TYPES_H__\n#define __MM_DAEMON_ENUMS_TYPES_H__\n" \
		--template $(top_srcdir)/build-aux/mm-enums-template.h \
		--ftail "#endif /* __MM_DAEMON_ENUMS_TYPES_H__ */\n" \
		$(DAEMON_ENUMS) > $@

mm-daemon-enums-types.c: Makefile.am $(top_srcdir)/build-aux/mm-enums-template.c mm-daemon-enums-types.h
	$(AM_V_GEN) glib-mkenums \
		--fhead "#include \"mm-daemon-enums-types.h\"" \
		--template $(top_srcdir)/build-aux/mm-enums-template.c \
		$(DAEMON_ENUMS) > $@

sbin_PROGRAMS = modem-manager

modem_manager_CPPFLAGS = \
	$(MM_CFLAGS) \
	$(GUDEV_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include \
	-I$(top_srcdir)/libmm-common \
	-I$(top_builddir)/libmm-common \
	-I${top_builddir}/marshallers \
	-DPLUGINDIR=\"$(pkglibdir)\"

if WITH_POLKIT
modem_manager_CPPFLAGS += $(POLKIT_CFLAGS)
endif

modem_manager_LDADD = \
	$(MM_LIBS) \
	$(GUDEV_LIBS) \
	$(top_builddir)/marshallers/libmarshallers.la \
	$(builddir)/libmodem-helpers.la \
	$(builddir)/libserial.la \
	$(top_builddir)/libqcdm/src/libqcdm.la

if WITH_POLKIT
modem_manager_LDADD += $(POLKIT_LIBS)
endif

auth_sources = \
	mm-auth-request.c \
	mm-auth-request.h \
	mm-auth-provider.h \
	mm-auth-provider.c \
	mm-auth-provider-factory.c

if WITH_POLKIT
auth_sources += \
	mm-auth-request-polkit.c \
	mm-auth-request-polkit.h \
	mm-auth-provider-polkit.c \
	mm-auth-provider-polkit.h
endif

modem_manager_SOURCES = \
	main.c \
	mm-log.c \
	mm-log.h \
	mm-daemon-enums-types.h \
	mm-daemon-enums-types.c \
	mm-private-boxed-types.h \
	mm-private-boxed-types.c \
	mm-callback-info.c \
	mm-callback-info.h \
	$(auth_sources) \
	mm-manager.c \
	mm-manager.h \
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
	mm-broadband-modem.h \
	mm-broadband-modem.c \
	mm-serial-parsers.c \
	mm-serial-parsers.h \
	mm-port-probe.h \
	mm-port-probe.c \
	mm-port-probe-at-command.h \
	mm-port-probe-at-command.c \
	mm-port-probe-cache.h \
	mm-port-probe-cache.c \
	mm-plugin.c \
	mm-plugin.h \
	mm-plugin-base.c \
	mm-plugin-base.h

CLEANFILES = \
	mm-private-enums-types.h \
	mm-private-enums-types.c
