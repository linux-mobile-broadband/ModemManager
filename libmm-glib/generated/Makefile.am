
SUBDIRS = . tests

AM_CFLAGS  = $(CODE_COVERAGE_CFLAGS)
AM_LDFLAGS = $(CODE_COVERAGE_LDFLAGS)

noinst_LTLIBRARIES = libmm-generated.la

GENERATED_H = \
	mm-enums-types.h \
	mm-errors-types.h \
	mm-gdbus-manager.h \
	mm-gdbus-sim.h \
	mm-gdbus-sms.h \
	mm-gdbus-call.h \
	mm-gdbus-bearer.h \
	mm-gdbus-modem.h \
	$(NULL)

GENERATED_C = \
	mm-enums-types.c \
	mm-errors-types.c \
	mm-errors-quarks.c \
	mm-gdbus-manager.c \
	mm-gdbus-sim.c \
	mm-gdbus-sms.c \
	mm-gdbus-call.c \
	mm-gdbus-bearer.c \
	mm-gdbus-modem.c   \
	$(NULL)

GENERATED_DOC = \
	mm-gdbus-doc-org.freedesktop.ModemManager1.xml \
	mm-gdbus-doc-org.freedesktop.ModemManager1.Sim.xml \
	mm-gdbus-doc-org.freedesktop.ModemManager1.Sms.xml \
	mm-gdbus-doc-org.freedesktop.ModemManager1.Call.xml \
	mm-gdbus-doc-org.freedesktop.ModemManager1.Bearer.xml \
	mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.xml \
	mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.Messaging.xml \
	mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.Voice.xml \
	mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.Location.xml \
	mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.Time.xml \
	mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.Firmware.xml \
	mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.Oma.xml \
	mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.ModemCdma.xml \
	mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.Modem3gpp.xml \
	mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.Modem3gpp.Ussd.xml \
	mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.Simple.xml \
	mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.Signal.xml \
	$(NULL)

BUILT_SOURCES = $(GENERATED_H) $(GENERATED_C) $(GENERATED_DOC)

# Enum types
mm-enums-types.h: Makefile.am $(top_srcdir)/include/ModemManager-enums.h $(top_srcdir)/build-aux/mm-enums-template.h
	$(AM_V_GEN) $(GLIB_MKENUMS) \
		--fhead "#include <ModemManager.h>\n#ifndef __MM_ENUMS_TYPES_H__\n#define __MM_ENUMS_TYPES_H__\n" \
		--template $(top_srcdir)/build-aux/mm-enums-template.h \
		--ftail "#endif /* __MM_ENUMS_TYPES_H__ */\n" \
		$(top_srcdir)/include/ModemManager-enums.h > $@

mm-enums-types.c: Makefile.am $(top_srcdir)/include/ModemManager-enums.h $(top_srcdir)/build-aux/mm-enums-template.c mm-enums-types.h
	$(AM_V_GEN) $(GLIB_MKENUMS) \
		--fhead "#include \"mm-enums-types.h\"\n" \
		--template $(top_srcdir)/build-aux/mm-enums-template.c \
		$(top_srcdir)/include/ModemManager-enums.h > $@

# Error types & quarks
mm-errors-types.h: Makefile.am $(top_srcdir)/include/ModemManager-errors.h $(top_srcdir)/build-aux/mm-errors-template.h
	$(AM_V_GEN) $(GLIB_MKENUMS) \
		--fhead "#ifndef __MM_ERRORS_TYPES_H__\n#define __MM_ERRORS_TYPES_H__\n" \
		--template $(top_srcdir)/build-aux/mm-errors-template.h \
		--ftail "#endif /* __MM_ERRORS_TYPES_H__ */\n" \
		$(top_srcdir)/include/ModemManager-errors.h > $@

mm-errors-types.c: Makefile.am $(top_srcdir)/include/ModemManager-errors.h $(top_srcdir)/build-aux/mm-errors-template.c mm-errors-types.h
	$(AM_V_GEN) $(GLIB_MKENUMS) \
		--fhead "#include <ModemManager.h>\n#include \"mm-errors-types.h\"\n" \
		--template $(top_srcdir)/build-aux/mm-errors-template.c \
		$(top_srcdir)/include/ModemManager-errors.h > $@

mm-errors-quarks.c: Makefile.am $(top_srcdir)/include/ModemManager-errors.h $(top_srcdir)/build-aux/mm-errors-quarks-template.c $(top_builddir)/include/ModemManager-names.h mm-errors-types.h
	$(AM_V_GEN) $(GLIB_MKENUMS) \
		--fhead "#include <ModemManager.h>\n#include \"mm-errors-types.h\"\n" \
		--template $(top_srcdir)/build-aux/mm-errors-quarks-template.c \
		$(top_srcdir)/include/ModemManager-errors.h > $@

# Manager interface
mm_gdbus_manager_generated = \
	mm-gdbus-manager.h \
	mm-gdbus-manager.c \
	mm-gdbus-doc-org.freedesktop.ModemManager1.xml \
	$(NULL)
mm_gdbus_manager_deps = \
	$(top_srcdir)/introspection/org.freedesktop.ModemManager1.xml \
	$(NULL)
mm-gdbus-manager.c: $(mm_gdbus_manager_deps)
	$(AM_V_GEN) $(GDBUS_CODEGEN) \
		--interface-prefix org.freedesktop.ModemManager1. \
		--c-namespace=MmGdbus \
		--generate-docbook mm-gdbus-doc \
		--generate-c-code mm-gdbus-manager \
		$< \
		$(NULL)
$(filter-out mm-gdbus-manager.c, $(mm_gdbus_manager_generated)): $(mm_gdbus_manager_deps) mm-gdbus-manager.c
	@: # nothing to do, generated as a side-effect of the .c

# Modem interfaces
mm_gdbus_modem_generated = \
	mm-gdbus-modem.h \
	mm-gdbus-modem.c \
	mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.xml \
	mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.Messaging.xml \
	mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.Voice.xml \
	mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.Location.xml \
	mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.Time.xml \
	mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.Firmware.xml \
	mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.Oma.xml \
	mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.ModemCdma.xml \
	mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.Modem3gpp.xml \
	mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.Modem3gpp.Ussd.xml \
	mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.Simple.xml \
	mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.Signal.xml \
	$(NULL)
mm_gdbus_modem_deps = \
	$(top_srcdir)/introspection/org.freedesktop.ModemManager1.Modem.xml \
	$(top_srcdir)/introspection/org.freedesktop.ModemManager1.Modem.Messaging.xml \
	$(top_srcdir)/introspection/org.freedesktop.ModemManager1.Modem.Voice.xml \
	$(top_srcdir)/introspection/org.freedesktop.ModemManager1.Modem.Location.xml \
	$(top_srcdir)/introspection/org.freedesktop.ModemManager1.Modem.Time.xml \
	$(top_srcdir)/introspection/org.freedesktop.ModemManager1.Modem.Firmware.xml \
	$(top_srcdir)/introspection/org.freedesktop.ModemManager1.Modem.Oma.xml \
	$(top_srcdir)/introspection/org.freedesktop.ModemManager1.Modem.ModemCdma.xml \
	$(top_srcdir)/introspection/org.freedesktop.ModemManager1.Modem.Modem3gpp.xml \
	$(top_srcdir)/introspection/org.freedesktop.ModemManager1.Modem.Modem3gpp.Ussd.xml \
	$(top_srcdir)/introspection/org.freedesktop.ModemManager1.Modem.Simple.xml \
	$(top_srcdir)/introspection/org.freedesktop.ModemManager1.Modem.Signal.xml \
	$(NULL)
mm-gdbus-modem.c: $(mm_gdbus_modem_deps)
	$(AM_V_GEN) $(GDBUS_CODEGEN) \
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
$(filter-out mm-gdbus-modem.c, $(mm_gdbus_modem_generated)): $(mm_gdbus_modem_deps) mm-gdbus-modem.c
	@: # nothing to do, generated as a side-effect of the .c

# SIM interface
mm_gdbus_sim_generated = \
	mm-gdbus-sim.h \
	mm-gdbus-sim.c \
	mm-gdbus-doc-org.freedesktop.ModemManager1.Sim.xml \
	$(NULL)
mm_gdbus_sim_deps = \
	$(top_srcdir)/introspection/org.freedesktop.ModemManager1.Sim.xml \
	$(NULL)
mm-gdbus-sim.c: $(mm_gdbus_sim_deps)
	$(AM_V_GEN) $(GDBUS_CODEGEN) \
		--interface-prefix org.freedesktop.ModemManager1. \
		--c-namespace=MmGdbus \
		--generate-docbook mm-gdbus-doc \
		--generate-c-code mm-gdbus-sim \
		$< \
		$(NULL)
$(filter-out mm-gdbus-sim.c, $(mm_gdbus_sim_generated)): $(mm_gdbus_sim_deps) mm-gdbus-sim.c
	@: # nothing to do, generated as a side-effect of the .c

# Bearer interface
mm_gdbus_bearer_generated = \
	mm-gdbus-bearer.h \
	mm-gdbus-bearer.c \
	mm-gdbus-doc-org.freedesktop.ModemManager1.Bearer.xml \
	$(NULL)
mm_gdbus_bearer_deps = \
	$(top_srcdir)/introspection/org.freedesktop.ModemManager1.Bearer.xml \
	$(NULL)
mm-gdbus-bearer.c: $(mm_gdbus_bearer_deps)
	$(AM_V_GEN) $(GDBUS_CODEGEN) \
		--interface-prefix org.freedesktop.ModemManager1. \
		--c-namespace=MmGdbus \
		--generate-docbook mm-gdbus-doc \
		--generate-c-code mm-gdbus-bearer \
		$< \
		$(NULL)
$(filter-out mm-gdbus-bearer.c, $(mm_gdbus_bearer_generated)): $(mm_gdbus_bearer_deps) mm-gdbus-bearer.c
	@: # nothing to do, generated as a side-effect of the .c

# SMS interface
mm_gdbus_sms_generated = \
	mm-gdbus-sms.h \
	mm-gdbus-sms.c \
	mm-gdbus-doc-org.freedesktop.ModemManager1.Sms.xml \
	$(NULL)
mm_gdbus_sms_deps = \
	$(top_srcdir)/introspection/org.freedesktop.ModemManager1.Sms.xml \
	$(NULL)
mm-gdbus-sms.c: $(mm_gdbus_sms_deps)
	$(AM_V_GEN) $(GDBUS_CODEGEN) \
		--interface-prefix org.freedesktop.ModemManager1. \
		--c-namespace=MmGdbus \
		--generate-docbook mm-gdbus-doc \
		--generate-c-code mm-gdbus-sms \
		--annotate "org.freedesktop.ModemManager1.Sms:Data" org.gtk.GDBus.C.ForceGVariant True \
		$< \
		$(NULL)
$(filter-out mm-gdbus-sms.c, $(mm_gdbus_sms_generated)): $(mm_gdbus_sms_deps) mm-gdbus-sms.c
	@: # nothing to do, generated as a side-effect of the .c

# Call interface
mm_gdbus_call_generated = \
	mm-gdbus-call.h \
	mm-gdbus-call.c \
	mm-gdbus-doc-org.freedesktop.ModemManager1.Call.xml \
	$(NULL)
mm_gdbus_call_deps = \
	$(top_srcdir)/introspection/org.freedesktop.ModemManager1.Call.xml \
	$(NULL)
mm-gdbus-call.c: $(mm_gdbus_call_deps)
	$(AM_V_GEN) $(GDBUS_CODEGEN) \
		--interface-prefix org.freedesktop.ModemManager1. \
		--c-namespace=MmGdbus \
		--generate-docbook mm-gdbus-doc \
		--generate-c-code mm-gdbus-call \
		$< \
		$(NULL)
$(filter-out mm-gdbus-call.c, $(mm_gdbus_call_generated)): $(mm_gdbus_call_deps) mm-gdbus-call.c
	@: # nothing to do, generated as a side-effect of the .c

nodist_libmm_generated_la_SOURCES = $(GENERATED_H) $(GENERATED_C)

libmm_generated_la_CPPFLAGS = \
	$(LIBMM_GLIB_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include \
	-Wno-unused-function \
	-Wno-float-equal \
	-Wno-shadow \
	$(NULL)

libmm_generated_la_CFLAGS = \
	$(WARN_CFLAGS) \
	$(NULL)

libmm_generated_la_LDFLAGS = \
	$(WARN_LDFLAGS) \
	$(LIBMM_GLIB_LIBS) \
	$(NULL)

includedir = @includedir@/libmm-glib
nodist_include_HEADERS = $(GENERATED_H)

CLEANFILES = $(GENERATED_H) $(GENERATED_C) $(GENERATED_DOC)
