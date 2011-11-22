
SUBDIRS = . marshallers build-aux include libmm-common libqcdm libwmc src plugins introspection po test docs

if WITH_POLKIT
SUBDIRS += policy
endif

dbusservicedir = $(DBUS_SYS_DIR)
dbusservice_DATA = org.freedesktop.ModemManager.conf

dbusservice_file_polkit = org.freedesktop.ModemManager.conf.polkit
dbusservice_file_nopolkit = org.freedesktop.ModemManager.conf.nopolkit

if WITH_POLKIT
org.freedesktop.ModemManager.conf: $(top_srcdir)/$(dbusservice_file_polkit)
	cp -f $(top_srcdir)/$(dbusservice_file_polkit) $(dbusservice_DATA)
else
org.freedesktop.ModemManager.conf: $(top_srcdir)/$(dbusservice_file_nopolkit)
	cp -f $(top_srcdir)/$(dbusservice_file_nopolkit) $(dbusservice_DATA)
endif

dbusactivationdir = $(datadir)/dbus-1/system-services
dbusactivation_in_files = org.freedesktop.ModemManager.service.in
dbusactivation_DATA = $(dbusactivation_in_files:.service.in=.service)

%service: %service.in
	$(edit) $< >$@

XMLS = $(wildcard introspection/*.xml)
xmldir = $(datadir)/dbus-1/interfaces
xml_DATA = $(filter-out introspection/all.xml introspection/org.freedesktop.DBus.Properties.xml, $(XMLS))

edit = @sed \
	-e 's|@sbindir[@]|$(sbindir)|g' \
	-e 's|@sysconfdir[@]|$(sysconfdir)|g' \
	-e 's|@localstatedir[@]|$(localstatedir)|g' \
	-e 's|@libexecdir[@]|$(libexecdir)|g'

DISTCHECK_CONFIGURE_FLAGS = \
	--with-udev-base-dir="$$dc_install_base" \
	--with-tests=yes \
	--with-docs=yes

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
	$(dbusactivation_in_files) \
	$(INTLTOOL_FILES) \
	$(dbusservice_file_polkit) \
	$(dbusservice_file_nopolkit)

ACLOCAL_AMFLAGS = -I m4
