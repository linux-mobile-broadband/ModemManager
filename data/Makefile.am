
# DBus Service file
dbusservicedir = $(DBUS_SYS_DIR)
dbusservice_DATA = org.freedesktop.ModemManager1.conf
dbusservice_file_polkit = org.freedesktop.ModemManager1.conf.polkit
dbusservice_file_nopolkit = org.freedesktop.ModemManager1.conf.nopolkit

if WITH_POLKIT
org.freedesktop.ModemManager1.conf: $(top_srcdir)/data/$(dbusservice_file_polkit)
	cp -f $(top_srcdir)/data/$(dbusservice_file_polkit) $(dbusservice_DATA)
else
org.freedesktop.ModemManager1.conf:  $(top_srcdir)/data/$(dbusservice_file_nopolkit)
	cp -f $(top_srcdir)/data/$(dbusservice_file_nopolkit) $(dbusservice_DATA)
endif


# DBus Activation file
edit = @sed \
       -e 's|@sbindir[@]|$(sbindir)|g' \
       -e 's|@sysconfdir[@]|$(sysconfdir)|g' \
       -e 's|@localstatedir[@]|$(localstatedir)|g' \
       -e 's|@libexecdir[@]|$(libexecdir)|g'
dbusactivationdir = $(datadir)/dbus-1/system-services
dbusactivation_in_files = org.freedesktop.ModemManager1.service.in
dbusactivation_DATA = $(dbusactivation_in_files:.service.in=.service)
%service: %service.in
	$(edit) $< >$@


# Icon
icondir=${datadir}/icons/hicolor/22x22/apps
icon_DATA = ModemManager.png


# Polkit
dist_polkit_policy_in_files = org.freedesktop.ModemManager1.policy.in
if WITH_POLKIT
polkit_policydir = $(datadir)/polkit-1/actions
dist_polkit_policy_DATA = $(dist_polkit_policy_in_files:.policy.in=.policy)
@INTLTOOL_POLICY_RULE@
endif


# Set up pkg-config .pc files for exported libraries
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = \
	ModemManager.pc \
	mm-common.pc \
	mm-glib.pc


DISTCLEANFILES = \
	$(dbusactivation_DATA) \
	$(dbusservice_DATA)

EXTRA_DIST = \
	$(dbusactivation_in_files) \
	$(dbusservice_file_polkit) \
	$(dbusservice_file_nopolkit) \
	$(icon_DATA) \
	$(dist_polkit_policy_in_files)
