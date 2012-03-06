bin_PROGRAMS = mmcli

mmcli_CPPFLAGS = \
	$(MMCLI_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include \
	-I$(top_srcdir)/libmm-common \
	-I$(top_builddir)/libmm-common \
	-I$(top_srcdir)/libmm-glib

mmcli_SOURCES = \
	mmcli.h \
	mmcli.c \
	mmcli-common.h \
	mmcli-common.c \
	mmcli-manager.c \
	mmcli-modem.c \
	mmcli-modem-3gpp.c \
	mmcli-modem-cdma.c \
	mmcli-modem-simple.c \
	mmcli-modem-location.c \
	mmcli-modem-messaging.c \
	mmcli-modem-time.c \
	mmcli-bearer.c \
	mmcli-sim.c \
	mmcli-sms.c

mmcli_LDADD = \
	$(MMCLI_LIBS) \
	$(top_builddir)/libmm-common/libmm-common.la \
	$(top_builddir)/libmm-glib/libmm-glib.la
