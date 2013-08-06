VAPIGEN_VAPIS = libmm-glib.vapi

if ENABLE_VAPIGEN
include $(VAPIGEN_MAKEFILE)

libmm-glib.vapi: $(top_builddir)/libmm-glib/ModemManager-1.0.gir libmm-glib.deps ModemManager-1.0.metadata

libmm_glib_vapi_METADATADIRS = $(srcdir)
libmm_glib_vapi_FILES = $(top_builddir)/libmm-glib/ModemManager-1.0.gir
libmm_glib_vapi_VAPIDIRS = $(builddir)
libmm_glib_vapi_DEPS = gio-2.0

vapidir = $(datadir)/vala/vapi
vapi_DATA = $(VAPIGEN_VAPIS) $(VAPIGEN_VAPIS:.vapi=.deps)

endif

CLEANFILES = $(VAPIGEN_VAPIS)

EXTRA_DIST = \
	ModemManager-1.0.metadata \
	libmm-glib.deps
