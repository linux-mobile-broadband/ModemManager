SUBDIRS=. tests

noinst_LTLIBRARIES = libmodem-helpers.la

libmodem_helpers_la_CPPFLAGS = \
	$(MM_CFLAGS)

libmodem_helpers_la_SOURCES = \
	mm-errors.c \
	mm-errors.h \
	mm-modem-helpers.c \
	mm-modem-helpers.h

sbin_PROGRAMS = modem-manager

modem_manager_CPPFLAGS = \
	$(MM_CFLAGS) \
	$(GUDEV_CFLAGS) \
	-I${top_builddir}/marshallers \
	-DPLUGINDIR=\"$(pkglibdir)\"

modem_manager_LDADD = \
	$(MM_LIBS) \
	$(GUDEV_LIBS) \
	$(top_builddir)/marshallers/libmarshallers.la \
	$(builddir)/libmodem-helpers.la

modem_manager_SOURCES = \
	main.c \
	mm-callback-info.c \
	mm-callback-info.h \
	mm-manager.c \
	mm-manager.h \
	mm-modem.c \
	mm-modem.h \
	mm-port.c \
	mm-port.h \
	mm-modem-base.c \
	mm-modem-base.h \
	mm-serial-port.c \
	mm-serial-port.h \
	mm-serial-parsers.c \
	mm-serial-parsers.h \
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

CLEANFILES = $(BUILT_SOURCES)
