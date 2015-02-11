
noinst_PROGRAMS = lsudev mmtty

lsudev_SOURCES = lsudev.c
lsudev_CPPFLAGS = $(GUDEV_CFLAGS)
lsudev_LDADD = $(GUDEV_LIBS)

mmtty_SOURCES = mmtty.c
mmtty_CPPFLAGS = \
	$(MM_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include \
	-I$(top_srcdir)/libmm-glib \
	-I$(top_srcdir)/libmm-glib/generated \
	-I$(top_builddir)/libmm-glib/generated
mmtty_LDADD = \
	$(MM_LIBS) \
	$(top_builddir)/src/libport.la \
	$(top_builddir)/src/libmodem-helpers.la

EXTRA_DIST = \
	mmcli-test-sms
