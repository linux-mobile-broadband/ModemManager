
# DBus Service file
dbusservicedir = $(DBUS_SYS_DIR)
dbusservice_DATA = org.freedesktop.ModemManager.conf
dbusservice_file_polkit = org.freedesktop.ModemManager.conf.polkit
dbusservice_file_nopolkit = org.freedesktop.ModemManager.conf.nopolkit

if WITH_POLKIT
org.freedesktop.ModemManager.conf: $(top_srcdir)/data/$(dbusservice_file_polkit)
	cp -f $(top_srcdir)/data/$(dbusservice_file_polkit) $(dbusservice_DATA)
else
org.freedesktop.ModemManager.conf:  $(top_srcdir)/data/$(dbusservice_file_nopolkit)
	cp -f $(top_srcdir)/data/$(dbusservice_file_nopolkit) $(dbusservice_DATA)
endif


# DBus Activation file
edit = @sed \
       -e 's|@sbindir[@]|$(sbindir)|g' \
       -e 's|@sysconfdir[@]|$(sysconfdir)|g' \
       -e 's|@localstatedir[@]|$(localstatedir)|g' \
       -e 's|@libexecdir[@]|$(libexecdir)|g'
dbusactivationdir = $(datadir)/dbus-1/system-services
dbusactivation_in_files = org.freedesktop.ModemManager.service.in
dbusactivation_DATA = $(dbusactivation_in_files:.service.in=.service)
%service: %service.in
	$(edit) $< >$@


# DBus Introspection files
XMLS = $(wildcard $(top_srcdir)/introspection/*.xml)
xmldir = $(datadir)/dbus-1/interfaces
xml_DATA = $(filter-out $(top_srcdir)/introspection/all.xml, $(XMLS))


# Icon
icondir=${datadir}/icons/hicolor/22x22/apps
icon_DATA = modem-manager.png


# Polkit
dist_polkit_policy_in_files = org.freedesktop.modem-manager.policy.in
if WITH_POLKIT
polkit_policydir = $(datadir)/polkit-1/actions
dist_polkit_policy_DATA = $(dist_polkit_policy_in_files:.policy.in=.policy)
@INTLTOOL_POLICY_RULE@
endif


DISTCLEANFILES = \
	$(dbusactivation_DATA) \
	$(dbusservice_DATA)

EXTRA_DIST = \
	$(dbusactivation_in_files) \
	$(dbusservice_file_polkit) \
	$(dbusservice_file_nopolkit) \
	$(icon_DATA) \
	$(dist_polkit_policy_in_files)

