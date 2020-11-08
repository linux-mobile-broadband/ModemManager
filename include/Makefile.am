XSLTPROC = xsltproc --xinclude --nonet

XMLS = $(wildcard $(top_srcdir)/introspection/*.xml)

includedir = @includedir@/ModemManager

include_HEADERS = \
	ModemManager-names.h \
	ModemManager-enums.h \
	ModemManager-errors.h \
	ModemManager-compat.h \
	ModemManager-version.h \
	ModemManager.h

ModemManager-names.h: $(XMLS) $(top_srcdir)/build-aux/header-generator.xsl
	$(AM_V_GEN) $(XSLTPROC) $(top_srcdir)/build-aux/header-generator.xsl $(top_srcdir)/introspection/all.xml > $@

MAINTAINERCLEANFILES = \
	ModemManager-names.h

EXTRA_DIST = \
	ModemManager-tags.h \
	ModemManager-version.h.in
