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
	mm-qcdm-serial-port.h

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
	mm-at.h \
	mm-at.c \
	mm-callback-info.c \
	mm-callback-info.h \
	$(auth_sources) \
	mm-manager.c \
	mm-manager.h \
	mm-plugin-manager.c \
	mm-plugin-manager.h \
	mm-sim.h \
	mm-sim.c \
	mm-base-modem.h \
	mm-base-modem.c \
	mm-iface-modem.h \
	mm-iface-modem.c \
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

