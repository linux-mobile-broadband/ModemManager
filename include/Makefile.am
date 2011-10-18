XSLTPROC = xsltproc --xinclude --nonet

XMLS = $(wildcard $(top_srcdir)/introspection/*.xml)

includedir = @includedir@/mm

include_HEADERS = \
	ModemManager.h

ModemManager.h: $(XMLS) $(top_srcdir)/introspection/all.xml $(top_srcdir)/build-aux/header-generator.xsl
	$(XSLTPROC) $(top_srcdir)/build-aux/header-generator.xsl $(top_srcdir)/introspection/all.xml > $@

CLEANFILES = \
	ModemManager.h