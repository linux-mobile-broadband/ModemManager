udevdir = $(UDEV_BASE_DIR)
udev_PROGRAMS = mm-modem-probe

mm_modem_probe_SOURCES = mm-modem-probe.c
mm_modem_probe_CPPFLAGS = $(MM_CFLAGS)
mm_modem_probe_LDADD = $(MM_LIBS)

udevrulesdir = $(UDEV_BASE_DIR)/rules.d
udevrules_DATA = 77-mm-modem-probe-capabilities.rules

EXTRA_DIST = \
	$(udevrules_DATA)

