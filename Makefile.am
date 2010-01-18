if WITH_DOCS

all:

XSLTPROC = xsltproc --xinclude --nonet

XMLS = $(wildcard introspection/mm-*.xml)
# Figure out if we need ASYNC_INTROSPECT and add it later

GENERATED_FILES = \
	docs/spec.html

docs/spec.html: $(XMLS) introspection/all.xml doc-generator.xsl
	@install -d docs
	$(XSLTPROC) doc-generator.xsl introspection/all.xml > $@

all: $(GENERATED_FILES)

CLEANFILES = $(GENERATED_FILES)
endif

SUBDIRS = marshallers src plugins introspection test

dbusservicedir = $(DBUS_SYS_DIR)
dbusservice_DATA = org.freedesktop.ModemManager.conf

dbusactivationdir = $(datadir)/dbus-1/system-services
dbusactivation_in_files = org.freedesktop.ModemManager.service.in
dbusactivation_DATA = $(dbusactivation_in_files:.service.in=.service)

%service: %service.in
	$(edit) $< >$@

edit = @sed \
	-e 's|@sbindir[@]|$(sbindir)|g' \
	-e 's|@sysconfdir[@]|$(sysconfdir)|g' \
	-e 's|@localstatedir[@]|$(localstatedir)|g' \
	-e 's|@libexecdir[@]|$(libexecdir)|g'

DISTCHECK_CONFIGURE_FLAGS = --with-udev-base-dir=$dc_install_base

DISTCLEANFILES = \
	$(dbusactivation_DATA)


EXTRA_DIST = \
	doc-generator.xsl \
	$(dbusservice_DATA) \
	$(dbusactivation_in_files)
