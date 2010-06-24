SUBDIRS=. tests

udevrulesdir = $(UDEV_BASE_DIR)/rules.d
udevrules_DATA = \
	77-mm-usb-device-blacklist.rules \
	77-mm-pcmcia-device-blacklist.rules \
	77-mm-platform-serial-whitelist.rules

EXTRA_DIST = \
	$(udevrules_DATA)

noinst_LTLIBRARIES = libmodem-helpers.la

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
	mm-utils.h

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
	$(top_builddir)/libqcdm/src/libqcdm.la \
	$(builddir)/libmodem-helpers.la

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

loc_sources = \
	mm-modem-location.c \
	mm-modem-location.h

modem_manager_SOURCES = \
	main.c \
	mm-callback-info.c \
	mm-callback-info.h \
	$(auth_sources) \
	mm-manager.c \
	mm-manager.h \
	mm-modem.c \
	mm-modem.h \
	mm-port.c \
	mm-port.h \
	mm-serial-port.c \
	mm-serial-port.h \
	mm-at-serial-port.c \
	mm-at-serial-port.h \
	mm-qcdm-serial-port.c \
	mm-qcdm-serial-port.h \
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
	mm-modem-simple.c \
	mm-modem-simple.h \
	mm-options.c \
	mm-options.h \
	mm-plugin.c \
	mm-plugin.h \
	mm-plugin-base.c \
	mm-plugin-base.h \
	mm-properties-changed-signal.c \
	mm-properties-changed-signal.h

mm-manager-glue.h: $(top_srcdir)/introspection/mm-manager.xml
	dbus-binding-tool --prefix=mm_manager --mode=glib-server --output=$@ $<

mm-modem-glue.h: $(top_srcdir)/introspection/mm-modem.xml
	dbus-binding-tool --prefix=mm_modem --mode=glib-server --output=$@ $<

mm-modem-simple-glue.h: $(top_srcdir)/introspection/mm-modem-simple.xml
	dbus-binding-tool --prefix=mm_modem_simple --mode=glib-server --output=$@ $<

mm-modem-cdma-glue.h: $(top_srcdir)/introspection/mm-modem-cdma.xml
	dbus-binding-tool --prefix=mm_modem_cdma --mode=glib-server --output=$@ $<

mm-modem-gsm-card-glue.h: $(top_srcdir)/introspection/mm-modem-gsm-card.xml
	dbus-binding-tool --prefix=mm_modem_gsm_card --mode=glib-server --output=$@ $<

mm-modem-gsm-network-glue.h: $(top_srcdir)/introspection/mm-modem-gsm-network.xml
	dbus-binding-tool --prefix=mm_modem_gsm_network --mode=glib-server --output=$@ $<

mm-modem-gsm-sms-glue.h: $(top_srcdir)/introspection/mm-modem-gsm-sms.xml
	dbus-binding-tool --prefix=mm_modem_gsm_sms --mode=glib-server --output=$@ $<

BUILT_SOURCES = \
	mm-manager-glue.h \
	mm-modem-glue.h \
	mm-modem-simple-glue.h \
	mm-modem-cdma-glue.h \
	mm-modem-gsm-card-glue.h \
	mm-modem-gsm-network-glue.h \
	mm-modem-gsm-sms-glue.h

if WITH_LOCATION_API
mm-modem-location-glue.h: $(top_srcdir)/introspection/mm-modem-location.xml
	dbus-binding-tool --prefix=mm_modem_location --mode=glib-server --output=$@ $<

modem_manager_SOURCES += $(loc_sources)

BUILT_SOURCES += mm-modem-location-glue.h
else
EXTRA_DIST += $(loc_sources)
endif

CLEANFILES = $(BUILT_SOURCES)
