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

SUBDIRS = marshallers libqcdm src plugins introspection po policy test

if WITH_POLKIT
SUBDIRS += policy
endif

dbusservicedir = $(DBUS_SYS_DIR)
dbusservice_DATA = org.freedesktop.ModemManager.conf

dbusservice_file_polkit = org.freedesktop.ModemManager.conf.polkit
dbusservice_file_nopolkit = org.freedesktop.ModemManager.conf.nopolkit

org.freedesktop.ModemManager.conf:
if WITH_POLKIT
	cp -f $(top_srcdir)/$(dbusservice_file_polkit) $(dbusservice_DATA)
else
	cp -f $(top_srcdir)/$(dbusservice_file_nopolkit) $(dbusservice_DATA)
endif

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

INTLTOOL_FILES = \
	intltool-extract.in \
	intltool-merge.in \
	intltool-update.in

DISTCLEANFILES = \
	$(dbusactivation_DATA) \
	$(dbusservice_DATA) \
	intltool-extract \
	intltool-merge \
	intltool-update \
	po/.intltool-merge-cache

EXTRA_DIST = \
	doc-generator.xsl \
	$(dbusactivation_in_files) \
	$(INTLTOOL_FILES) \
	$(dbusservice_file_polkit) \
	$(dbusservice_file_nopolkit)

ACLOCAL_AMFLAGS = -I m4
