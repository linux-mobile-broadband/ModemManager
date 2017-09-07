
# Logos
LOGOS_PNG = \
	$(top_srcdir)/data/ModemManager-logo-square.png \
	$(top_srcdir)/data/ModemManager-logo-wide.png \
	$(top_srcdir)/data/ModemManager-logo-wide-text.png

# Diagrams
DIAGRAMS_PNG = \
	$(top_srcdir)/data/ModemManager-states.png \
	$(top_srcdir)/data/ModemManager-interface-initialization-sequence.png \
	$(top_srcdir)/data/ModemManager-interface-initialization-sequence-subclassed.png

# The name of the module.
DOC_MODULE = ModemManager

# The top-level SGML file.
DOC_MAIN_SGML_FILE = $(DOC_MODULE)-docs.xml

# Extra options to supply to gtkdoc-scan
SCAN_OPTIONS = --deprecated-guards="MM_DISABLE_DEPRECATED"

# The directory containing the source code.
DOC_SOURCE_DIR = $(top_srcdir)/include

HFILE_GLOB =
CFILE_GLOB =

# Headers to ignore
IGNORE_HFILES = \
	ModemManager.h \
	ModemManager-names.h \
	$(NULL)

# CFLAGS and LDFLAGS for compiling scan program. Only needed
# if $(DOC_MODULE).types is non-empty.
AM_CPPFLAGS = \
	-I$(srcdir) \
	-I$(top_srcdir) \
	-I$(top_builddir) \
	$(MM_CFLAGS) \
	$(NULL)

GTKDOC_LIBS = \
	$(MM_LIBS) \
	$(NULL)

# Extra options to supply to gtkdoc-mkdb
MKDB_OPTIONS = --output-format=xml --sgml-mode --name-space=mm

# Images to copy into HTML directory
HTML_IMAGES = \
	$(DIAGRAMS_PNG) \
	$(LOGOS_PNG) \
	$(NULL)

content_files =
expand_content_files = \
	ModemManager-overview.xml \
	ModemManager-dbus-reference.xml \
	ModemManager-migration-reference.xml \
	$(top_builddir)/libmm-glib/generated/mm-gdbus-doc-org.freedesktop.ModemManager1.xml \
	$(top_builddir)/libmm-glib/generated/mm-gdbus-doc-org.freedesktop.ModemManager1.Sim.xml \
	$(top_builddir)/libmm-glib/generated/mm-gdbus-doc-org.freedesktop.ModemManager1.Sms.xml \
	$(top_builddir)/libmm-glib/generated/mm-gdbus-doc-org.freedesktop.ModemManager1.Bearer.xml \
	$(top_builddir)/libmm-glib/generated/mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.xml \
	$(top_builddir)/libmm-glib/generated/mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.Messaging.xml \
	$(top_builddir)/libmm-glib/generated/mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.Location.xml \
	$(top_builddir)/libmm-glib/generated/mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.Time.xml \
	$(top_builddir)/libmm-glib/generated/mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.Firmware.xml \
	$(top_builddir)/libmm-glib/generated/mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.Oma.xml \
	$(top_builddir)/libmm-glib/generated/mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.ModemCdma.xml \
	$(top_builddir)/libmm-glib/generated/mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.Modem3gpp.xml \
	$(top_builddir)/libmm-glib/generated/mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.Modem3gpp.Ussd.xml \
	$(top_builddir)/libmm-glib/generated/mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.Simple.xml \
	$(top_builddir)/libmm-glib/generated/mm-gdbus-doc-org.freedesktop.ModemManager1.Modem.Signal.xml \
	$(NULL)

extra_files = \
	$(NULL)

if ENABLE_GTK_DOC
include $(top_srcdir)/gtk-doc.make
else
EXTRA_DIST =
CLEANFILES =
endif

EXTRA_DIST += \
	ModemManager-overview.xml \
	ModemManager-dbus-reference.xml \
	ModemManager-migration-reference.xml \
	version.xml.in \
	$(DIAGRAMS) \
	$(NULL)

CLEANFILES += \
	$(DOC_MODULE)-decl-list.txt \
	$(DOC_MODULE)-decl.txt \
	$(DOC_MODULE)-overrides.txt \
	$(DOC_MODULE)-undeclared.txt \
	$(DOC_MODULE)-undocumented.txt \
	$(DOC_MODULE)-overrides.txt \
	$(DOC_MODULE)-unused.txt \
	$(DOC_MODULE).args \
	$(DOC_MODULE).hierarchy \
	$(DOC_MODULE).interfaces \
	$(DOC_MODULE).prerequisites \
	$(DOC_MODULE).signals \
	*.stamp \
	-rf xml html tmpl \
	$(NULL)
