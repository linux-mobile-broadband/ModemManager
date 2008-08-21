sbin_PROGRAMS = modem-manager

modem_manager_CPPFLAGS = \
	$(MM_CFLAGS) \
	-DPLUGINDIR=\"$(pkglibdir)\"

modem_manager_LDADD = \
	$(MM_LIBS)

modem_manager_SOURCES = \
	main.c \
	mm-callback-info.c \
	mm-callback-info.h \
	mm-cdma-modem.c \
	mm-cdma-modem.h \
	mm-generic-cdma.c \
	mm-generic-cdma.h \
	mm-generic-gsm.c \
	mm-generic-gsm.h \
	mm-gsm-modem.c \
	mm-gsm-modem.h \
	mm-manager.c \
	mm-manager.h \
	mm-modem.c \
	mm-modem.h \
	mm-modem-error.c \
	mm-modem-error.h \
	mm-plugin.c \
	mm-plugin.h \
	mm-serial.c \
	mm-serial.h

mm-manager-glue.h: $(top_srcdir)/introspection/mm-manager.xml
	dbus-binding-tool --prefix=mm_manager --mode=glib-server --output=$@ $<

mm-modem-glue.h: $(top_srcdir)/introspection/mm-modem.xml
	dbus-binding-tool --prefix=mm_modem --mode=glib-server --output=$@ $<

mm-gsm-modem-glue.h: $(top_srcdir)/introspection/mm-gsm-modem.xml
	dbus-binding-tool --prefix=mm_gsm_modem --mode=glib-server --output=$@ $<

mm-cdma-modem-glue.h: $(top_srcdir)/introspection/mm-cdma-modem.xml
	dbus-binding-tool --prefix=mm_cdma_modem --mode=glib-server --output=$@ $<

BUILT_SOURCES = \
	mm-cdma-modem-glue.h \
	mm-gsm-modem-glue.h \
	mm-manager-glue.h \
	mm-modem-glue.h

CLEANFILES = $(BUILT_SOURCES)
