pkglib_LTLIBRARIES = \
	libmm-plugin-huawei.la \
	libmm-plugin-hso.la \
	libmm-plugin-mbm.la \
	libmm-plugin-option.la \
	libmm-plugin-sierra.la \
	libmm-plugin-novatel.la

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
	mm-modem-gsm-hso-glue.h \
	mm-modem-hso.c \
	mm-modem-hso.h \
	mm-plugin-hso.c \
	mm-plugin-hso.h

mm-modem-gsm-hso-glue.h: $(top_srcdir)/introspection/mm-modem-gsm-hso.xml
	dbus-binding-tool --prefix=mm_modem_gsm_hso --mode=glib-server --output=$@ $<

libmm_plugin_hso_la_CPPFLAGS = \
	$(MM_CFLAGS) \
	-I$(top_srcdir)/src

libmm_plugin_hso_la_LDFLAGS = -module -avoid-version

# MBM

libmm_plugin_mbm_la_SOURCES = \
	mm-modem-mbm.c \
	mm-modem-mbm.h \
	mm-plugin-mbm.c \
	mm-plugin-mbm.h

libmm_plugin_mbm_la_CPPFLAGS = \
	$(MM_CFLAGS) \
	-I$(top_srcdir)/src

libmm_plugin_mbm_la_LDFLAGS = -module -avoid-version

# Option

libmm_plugin_option_la_SOURCES = \
	mm-modem-option.c \
	mm-modem-option.h \
	mm-plugin-option.c \
	mm-plugin-option.h

libmm_plugin_option_la_CPPFLAGS = \
	$(MM_CFLAGS) \
	-I$(top_srcdir)/src

libmm_plugin_option_la_LDFLAGS = -module -avoid-version

# Sierra

libmm_plugin_sierra_la_SOURCES = \
	mm-modem-sierra.c \
	mm-modem-sierra.h \
	mm-plugin-sierra.c \
	mm-plugin-sierra.h

libmm_plugin_sierra_la_CPPFLAGS = \
	$(MM_CFLAGS) \
	-I$(top_srcdir)/src

libmm_plugin_sierra_la_LDFLAGS = -module -avoid-version

# Novatel

libmm_plugin_novatel_la_SOURCES = \
	mm-modem-novatel-cdma.c \
	mm-modem-novatel-cdma.h \
	mm-modem-novatel-gsm.c \
	mm-modem-novatel-gsm.h \
	mm-plugin-novatel.c \
	mm-plugin-novatel.h

libmm_plugin_novatel_la_CPPFLAGS = \
	$(MM_CFLAGS) \
	-I$(top_srcdir)/src

libmm_plugin_novatel_la_LDFLAGS = -module -avoid-version


BUILT_SOURCES = \
	mm-modem-gsm-hso-glue.h

CLEANFILES = $(BUILT_SOURCES)
