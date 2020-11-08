
# Logos
LOGOS_PNG = \
	$(top_srcdir)/data/ModemManager-logo-square.png \
	$(top_srcdir)/data/ModemManager-logo-wide.png \
	$(top_srcdir)/data/ModemManager-logo-wide-text.png

# The name of the module.
DOC_MODULE = libmm-glib

# The top-level SGML file.
DOC_MAIN_SGML_FILE = $(DOC_MODULE)-docs.xml

# Extra options to supply to gtkdoc-scan
SCAN_OPTIONS = --rebuild-types --deprecated-guards="MM_DISABLE_DEPRECATED"

# The directory containing the source code.
DOC_SOURCE_DIR = \
	$(top_srcdir)/libmm-glib \
	$(top_srcdir)/libmm-glib/generated \
	$(top_builddir)/libmm-glib/generated \
	$(NULL)

HFILE_GLOB =
CFILE_GLOB =

# Headers to ignore
IGNORE_HFILES = \
	mm-helpers.h \
	mm-common-helpers.h \
	mm-gdbus-test.h \
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
	$(top_builddir)/libmm-glib/libmm-glib.la \
	$(MM_LIBS) \
	$(NULL)

# Extra options to supply to gtkdoc-mkdb
MKDB_OPTIONS = \
	--output-format=xml \
	--sgml-mode \
	--name-space=mm \
	--ignore-files=mm-gdbus-test.h \
	--ignore-files=mm-gdbus-test.c \
	$(NULL)

# Images to copy into HTML directory
HTML_IMAGES = \
	$(LOGOS_PNG) \
	$(NULL)

content_files = \
	$(NULL)

if ENABLE_GTK_DOC
include $(top_srcdir)/gtk-doc.make
else
EXTRA_DIST =
CLEANFILES =
endif

EXTRA_DIST += \
	version.xml.in \
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
	$(DOC_MODULE).actions \
	*.stamp \
	-rf xml html tmpl \
	$(NULL)
