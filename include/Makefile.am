XSLTPROC = xsltproc --xinclude --nonet

XMLS = $(wildcard $(top_srcdir)/new/*.xml)

includedir = @includedir@/mm

include_HEADERS = \
	ModemManager-names.h \
	ModemManager-enums.h \
	ModemManager-errors.h \
	ModemManager1.h \
	ModemManager.h

ModemManager-names.h: $(XMLS) $(top_srcdir)/new/all.xml $(top_srcdir)/build-aux/header-generator.xsl
	$(AM_V_GEN) $(XSLTPROC) $(top_srcdir)/build-aux/header-generator.xsl $(top_srcdir)/new/all.xml > $@

CLEANFILES = \
	ModemManager-names.h
