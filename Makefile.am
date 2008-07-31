SUBDIRS = src plugins introspection

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = ModemManager.pc

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

DISTCLEANFILES = \
	ModemManager.pc \
	$(dbusactivation_DATA)


EXTRA_DIST = \
	ModemManager.pc.in \
	$(dbusservice_DATA) \
	$(dbusactivation_in_files)
