XSLTPROC = xsltproc --xinclude --nonet

OLD_XMLS = $(wildcard $(top_srcdir)/introspection/*.xml)
NEW_XMLS = $(wildcard $(top_srcdir)/new/*.xml)

includedir = @includedir@/mm

include_HEADERS = \
	ModemManager-names.h \
	ModemManager-enums.h \
	ModemManager-errors.h \
	ModemManager1.h \
	ModemManager.h

# NOTE: This is the header of the 0.5 API, which will get removed
ModemManager.h: $(OLD_XMLS) $(top_srcdir)/introspection/all.xml $(top_srcdir)/build-aux/header-generator.xsl
	$(AM_V_GEN) $(XSLTPROC) $(top_srcdir)/build-aux/header-generator.xsl $(top_srcdir)/introspection/all.xml > $@

ModemManager-names.h: $(NEW_XMLS) $(top_srcdir)/new/all.xml $(top_srcdir)/build-aux/header-generator-new.xsl
	$(AM_V_GEN) $(XSLTPROC) $(top_srcdir)/build-aux/header-generator-new.xsl $(top_srcdir)/new/all.xml > $@

CLEANFILES = \
	ModemManager-names.h \
	ModemManager.h
