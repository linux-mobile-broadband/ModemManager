if HAVE_PPPD_H

pppd_plugindir = $(PPPD_PLUGIN_DIR)
pppd_plugin_LTLIBRARIES = mm-test-pppd-plugin.la

mm_test_pppd_plugin_la_SOURCES = \
	mm-test-pppd-plugin.c

mm_test_pppd_plugin_la_CPPFLAGS = $(MM_CFLAGS)
mm_test_pppd_plugin_la_LDFLAGS = -module -avoid-version
mm_test_pppd_plugin_la_LIBADD = $(MM_LIBS)

endif

noinst_PROGRAMS = lsudev
lsudev_SOURCES = lsudev.c
lsudev_CPPFLAGS = $(GUDEV_CFLAGS)
lsudev_LDADD = $(GUDEV_LIBS)


EXTRA_DIST = \
	mm-test.py \
	disable.py \
	enable.py \
	info.py \
	list-modems.py \
	location.py \
	mm-send-sms.py

