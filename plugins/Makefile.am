pkglib_LTLIBRARIES = \
	libmm-plugin-huawei.la \
	libmm-plugin-hso.la

# Huawei

libmm_plugin_huawei_la_SOURCES = \
	mm-modem-huawei.c \
	mm-modem-huawei.h \
	mm-plugin-huawei.c \
	mm-plugin-huawei.h

libmm_plugin_huawei_la_CPPFLAGS = \
	$(MM_CFLAGS) \
	-I$(top_srcdir)/src

libmm_plugin_huawei_la_LDFLAGS = -module -avoid-version

# HSO

libmm_plugin_hso_la_SOURCES = \
	mm-gsm-modem-hso-glue.h \
	mm-modem-hso.c \
	mm-modem-hso.h \
	mm-plugin-hso.c \
	mm-plugin-hso.h

mm-gsm-modem-hso-glue.h: $(top_srcdir)/introspection/mm-gsm-modem-hso.xml
	dbus-binding-tool --prefix=mm_gsm_modem_hso --mode=glib-server --output=$@ $<

libmm_plugin_hso_la_CPPFLAGS = \
	$(MM_CFLAGS) \
	-I$(top_srcdir)/src

libmm_plugin_hso_la_LDFLAGS = -module -avoid-version

BUILT_SOURCES = \
	mm-gsm-modem-hso-glue.h

CLEANFILES = $(BUILT_SOURCES)
