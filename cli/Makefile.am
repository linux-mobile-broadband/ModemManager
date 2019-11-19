bin_PROGRAMS = mmcli

mmcli_CPPFLAGS = \
	$(WARN_CFLAGS) \
	$(MMCLI_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include \
	-I$(top_srcdir)/libmm-glib \
	-I${top_srcdir}/libmm-glib/generated \
	-I${top_builddir}/libmm-glib/generated \
	$(NULL)

mmcli_SOURCES = \
	mmcli.h \
	mmcli.c \
	mmcli-common.h mmcli-common.c \
	mmcli-output.h mmcli-output.c \
	mmcli-manager.c \
	mmcli-modem.c \
	mmcli-modem-3gpp.c \
	mmcli-modem-cdma.c \
	mmcli-modem-simple.c \
	mmcli-modem-location.c \
	mmcli-modem-messaging.c \
	mmcli-modem-voice.c \
	mmcli-modem-time.c \
	mmcli-modem-firmware.c \
	mmcli-modem-signal.c \
	mmcli-modem-oma.c \
	mmcli-bearer.c \
	mmcli-sim.c \
	mmcli-sms.c \
	mmcli-call.c \
	$(NULL)

mmcli_LDADD = \
	$(top_builddir)/libmm-glib/libmm-glib.la \
	$(NULL)

mmcli_LDFLAGS = \
	$(WARN_LDFLAGS) \
	$(MMCLI_LIBS) \
	$(NULL)

if WITH_UDEV
mmcli_CPPFLAGS += $(GUDEV_CFLAGS)
mmcli_LDFLAGS  += $(GUDEV_LIBS)
endif

completiondir = $(datadir)/bash-completion/completions

install-data-hook:
	$(mkinstalldirs) $(DESTDIR)$(completiondir)
	$(INSTALL_DATA) $(srcdir)/mmcli-completion $(DESTDIR)$(completiondir)/mmcli

uninstall-hook:
	rm -f $(DESTDIR)$(completiondir)/mmcli

EXTRA_DIST = mmcli-completion
