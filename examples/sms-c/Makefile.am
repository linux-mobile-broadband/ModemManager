noinst_PROGRAMS = sms-c-sync

sms_c_sync_CPPFLAGS = \
	$(WARN_CFLAGS) \
	$(MMCLI_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include \
	-I$(top_srcdir)/libmm-glib \
	-I${top_srcdir}/libmm-glib/generated \
	-I${top_builddir}/libmm-glib/generated \
	$(NULL)

sms_c_sync_SOURCES = \
	sms-c-sync.c \
	$(NULL)

sms_c_sync_LDADD = \
	$(top_builddir)/libmm-glib/libmm-glib.la \
	$(NULL)

sms_c_sync_LDFLAGS = \
	$(WARN_LDFLAGS) \
	$(MMCLI_LIBS) \
	$(NULL)
