
noinst_LTLIBRARIES = libmm-common.la

GENERATED_H = \
	mm-enums-types.h \
	mm-errors-types.h \
	mm-gdbus-manager.h \
	mm-gdbus-sim.h \
	mm-gdbus-sms.h \
	mm-gdbus-bearer.h \
	mm-gdbus-modem.h

GENERATED_C = \
	mm-enums-types.c \
	mm-errors-types.c \
	mm-gdbus-manager.c \
	mm-gdbus-sim.c \
	mm-gdbus-sms.c \
	mm-gdbus-bearer.c \
	mm-gdbus-modem.c

GENERATED_DOC = \
	mm-gdbus-doc-org.freedesktop.ModemManager1.xml \
	mm-gdbus-doc-org.freedesktop.ModemManager1.Sim.xml \
	mm-gdbus-doc-org.freedesktop.ModemManager1.Sms.xml \
	mm-gdbus-doc-org.freedesktop.ModemManager1.Bearer.xml \
	mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.xml \
	mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.Messaging.xml \
	mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.Location.xml \
	mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.Firmware.xml \
	mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.Contacts.xml \
	mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.ModemCdma.xml \
	mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.Modem3gpp.xml \
	mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.Modem3gpp.Ussd.xml \
	mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.Simple.xml

# Enum types
mm-enums-types.h: $(top_srcdir)/include/ModemManager-enums.h $(top_srcdir)/build-aux/mm-enums-types.h.template
	$(AM_V_GEN) glib-mkenums \
		--template $(top_srcdir)/build-aux/mm-enums-types.h.template \
		$(top_srcdir)/include/ModemManager-enums.h > $@

mm-enums-types.c: $(top_srcdir)/include/ModemManager-enums.h $(top_srcdir)/build-aux/mm-enums-types.c.template mm-enums-types.h
	$(AM_V_GEN) glib-mkenums \
		--template $(top_srcdir)/build-aux/mm-enums-types.c.template \
		$(top_srcdir)/include/ModemManager-enums.h > $@

# Error types
mm-errors-types.h: $(top_srcdir)/include/ModemManager-errors.h $(top_srcdir)/build-aux/mm-errors-types.h.template
	$(AM_V_GEN) glib-mkenums \
		--template $(top_srcdir)/build-aux/mm-errors-types.h.template \
		$(top_srcdir)/include/ModemManager-errors.h > $@

mm-errors-types.c: $(top_srcdir)/include/ModemManager-errors.h $(top_srcdir)/build-aux/mm-errors-types.c.template mm-errors-types.h
	$(AM_V_GEN) glib-mkenums \
		--template $(top_srcdir)/build-aux/mm-errors-types.c.template \
		$(top_srcdir)/include/ModemManager-errors.h > $@

# Manager interface
mm_gdbus_manager_generated = \
	mm-gdbus-manager.h \
	mm-gdbus-manager.c \
	mm-gdbus-doc-org.freedesktop.ModemManager1.xml
$(mm_gdbus_manager_generated): $(top_srcdir)/new/org.freedesktop.ModemManager1.xml
	$(AM_V_GEN) gdbus-codegen \
		--interface-prefix org.freedesktop.ModemManager1. \
		--c-namespace=MmGdbus \
		--generate-docbook mm-gdbus-doc \
		--generate-c-code mm-gdbus-manager \
		$< \
		$(NULL)

# SIM interface
mm_gdbus_sim_generated = \
	mm-gdbus-sim.h \
	mm-gdbus-sim.c \
	mm-gdbus-doc-org.freedesktop.ModemManager1.Sim.xml
$(mm_gdbus_sim_generated): $(top_srcdir)/new/org.freedesktop.ModemManager1.Sim.xml
	$(AM_V_GEN) gdbus-codegen \
		--interface-prefix org.freedesktop.ModemManager1. \
		--c-namespace=MmGdbus \
		--generate-docbook mm-gdbus-doc \
		--generate-c-code mm-gdbus-sim \
		$< \
		$(NULL)

# Modem interfaces
mm_gdbus_modem_generated = \
	mm-gdbus-modem.h \
	mm-gdbus-modem.c \
	mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.xml
mm_gdbus_modem_deps = \
	$(top_srcdir)/new/org.freedesktop.ModemManager1.Modem.xml \
	$(top_srcdir)/new/org.freedesktop.ModemManager1.Modem.Messaging.xml \
	$(top_srcdir)/new/org.freedesktop.ModemManager1.Modem.Location.xml \
	$(top_srcdir)/new/org.freedesktop.ModemManager1.Modem.Firmware.xml \
	$(top_srcdir)/new/org.freedesktop.ModemManager1.Modem.Contacts.xml \
	$(top_srcdir)/new/org.freedesktop.ModemManager1.Modem.ModemCdma.xml \
	$(top_srcdir)/new/org.freedesktop.ModemManager1.Modem.Modem3gpp.xml \
	$(top_srcdir)/new/org.freedesktop.ModemManager1.Modem.Modem3gpp.Ussd.xml \
	$(top_srcdir)/new/org.freedesktop.ModemManager1.Modem.Simple.xml
$(mm_gdbus_modem_generated): $(mm_gdbus_modem_deps)
	$(AM_V_GEN) gdbus-codegen \
		--interface-prefix org.freedesktop.ModemManager1. \
		--c-namespace=MmGdbus \
		--generate-docbook mm-gdbus-doc \
		--generate-c-code mm-gdbus-modem \
		--c-generate-object-manager \
		--annotate "org.freedesktop.ModemManager1.Modem.ModemCdma" org.gtk.GDBus.C.Name ModemCdma \
		--annotate "org.freedesktop.ModemManager1.Modem.Modem3gpp" org.gtk.GDBus.C.Name Modem3gpp \
		--annotate "org.freedesktop.ModemManager1.Modem.Modem3gpp.Ussd" org.gtk.GDBus.C.Name Modem3gppUssd \
		$^ \
		$(NULL)

# SMS interface
mm_gdbus_sms_generated = \
	mm-gdbus-sms.h \
	mm-gdbus-sms.c \
	mm-gdbus-doc-org.freedesktop.ModemManager1.Sms.xml
$(mm_gdbus_sms_generated): $(top_srcdir)/new/org.freedesktop.ModemManager1.Sms.xml
	$(AM_V_GEN) gdbus-codegen \
		--interface-prefix org.freedesktop.ModemManager1. \
		--c-namespace=MmGdbus \
		--generate-docbook mm-gdbus-doc \
		--generate-c-code mm-gdbus-sms \
		$< \
		$(NULL)

# Bearer interface
mm_gdbus_bearer_generated = \
	mm-gdbus-bearer.h \
	mm-gdbus-bearer.c \
	mm-gdbus-doc-org.freedesktop.ModemManager1.Bearer.xml
$(mm_gdbus_bearer_generated): $(top_srcdir)/new/org.freedesktop.ModemManager1.Bearer.xml
	$(AM_V_GEN) gdbus-codegen \
		--interface-prefix org.freedesktop.ModemManager1. \
		--c-namespace=MmGdbus \
		--generate-docbook mm-gdbus-doc \
		--generate-c-code mm-gdbus-bearer \
		$< \
		$(NULL)

libmm_common_la_SOURCES = \
	$(GENERATED_H) \
	$(GENERATED_C) \
	mm-errors-quarks.c \
	mm-common-helpers.h \
	mm-common-helpers.c

libmm_common_la_CPPFLAGS = \
	$(MM_CFLAGS) \
	-I$(top_srcdir)/include \
	-Wno-unused-function \
	-Wno-float-equal \
	-Wno-shadow

CLEANFILES = $(GENERATED_H) $(GENERATED_C) $(GENERATED_DOC)
