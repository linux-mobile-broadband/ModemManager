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
	$(MM_CFLAGS)

libmodem_helpers_la_SOURCES = \
	mm-errors.c \
	mm-errors.h \
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
	-I$(top_srcdir)

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
	mm-callback-info.c \
	mm-callback-info.h \
	$(auth_sources) \
	mm-manager.c \
	mm-manager.h \
	mm-modem.c \
	mm-modem.h \
	mm-serial-parsers.c \
	mm-serial-parsers.h \
	mm-modem-base.c \
	mm-modem-base.h \
	mm-generic-cdma.c \
	mm-generic-cdma.h \
	mm-generic-gsm.c \
	mm-generic-gsm.h \
	mm-modem-cdma.c \
	mm-modem-cdma.h \
	mm-modem-gsm.h \
	mm-modem-gsm-card.c \
	mm-modem-gsm-card.h \
	mm-modem-gsm-network.c \
	mm-modem-gsm-network.h \
	mm-modem-gsm-sms.c \
	mm-modem-gsm-sms.h \
	mm-modem-gsm-ussd.c \
	mm-modem-gsm-ussd.h \
	mm-modem-simple.c \
	mm-modem-simple.h \
	mm-plugin.c \
	mm-plugin.h \
	mm-plugin-base.c \
	mm-plugin-base.h \
	mm-properties-changed-signal.c \
	mm-properties-changed-signal.h \
	mm-modem-location.c \
	mm-modem-location.h

mm-manager-glue.h: $(top_srcdir)/introspection/org.freedesktop.ModemManager.xml
	$(AM_V_GEN) dbus-binding-tool --prefix=mm_manager --mode=glib-server --output=$@ $<

mm-modem-glue.h: $(top_srcdir)/introspection/org.freedesktop.ModemManager.Modem.xml
	$(AM_V_GEN) dbus-binding-tool --prefix=mm_modem --mode=glib-server --output=$@ $<

mm-modem-simple-glue.h: $(top_srcdir)/introspection/org.freedesktop.ModemManager.Modem.Simple.xml
	$(AM_V_GEN) dbus-binding-tool --prefix=mm_modem_simple --mode=glib-server --output=$@ $<

mm-modem-cdma-glue.h: $(top_srcdir)/introspection/org.freedesktop.ModemManager.Modem.Cdma.xml
	$(AM_V_GEN) dbus-binding-tool --prefix=mm_modem_cdma --mode=glib-server --output=$@ $<

mm-modem-gsm-card-glue.h: $(top_srcdir)/introspection/org.freedesktop.ModemManager.Modem.Gsm.Card.xml
	$(AM_V_GEN) dbus-binding-tool --prefix=mm_modem_gsm_card --mode=glib-server --output=$@ $<

mm-modem-gsm-network-glue.h: $(top_srcdir)/introspection/org.freedesktop.ModemManager.Modem.Gsm.Network.xml
	$(AM_V_GEN) dbus-binding-tool --prefix=mm_modem_gsm_network --mode=glib-server --output=$@ $<

mm-modem-gsm-sms-glue.h: $(top_srcdir)/introspection/org.freedesktop.ModemManager.Modem.Gsm.SMS.xml
	$(AM_V_GEN) dbus-binding-tool --prefix=mm_modem_gsm_sms --mode=glib-server --output=$@ $<

mm-modem-gsm-ussd-glue.h: $(top_srcdir)/introspection/org.freedesktop.ModemManager.Modem.Gsm.Ussd.xml
	$(AM_V_GEN) dbus-binding-tool --prefix=mm_modem_gsm_ussd --mode=glib-server --output=$@ $<

mm-properties-changed-glue.h: $(top_srcdir)/introspection/org.freedesktop.DBus.Properties.xml
	$(AM_V_GEN) dbus-binding-tool --prefix=mm_properties_changed --mode=glib-server --output=$@ $<

BUILT_SOURCES = \
	mm-manager-glue.h \
	mm-modem-glue.h \
	mm-modem-simple-glue.h \
	mm-modem-cdma-glue.h \
	mm-modem-gsm-card-glue.h \
	mm-modem-gsm-network-glue.h \
	mm-modem-gsm-sms-glue.h \
	mm-modem-gsm-ussd-glue.h \
	mm-properties-changed-glue.h

mm-modem-location-glue.h: $(top_srcdir)/introspection/org.freedesktop.ModemManager.Modem.Location.xml
	$(AM_V_GEN) dbus-binding-tool --prefix=mm_modem_location --mode=glib-server --output=$@ $<

modem_manager_SOURCES += $(loc_sources)

BUILT_SOURCES += mm-modem-location-glue.h

CLEANFILES = $(BUILT_SOURCES)
