
SUBDIRS = . tests

edit = @sed \
       -e 's|@sbindir[@]|$(sbindir)|g' \
       -e 's|@sysconfdir[@]|$(sysconfdir)|g' \
       -e 's|@localstatedir[@]|$(localstatedir)|g' \
       -e 's|@libexecdir[@]|$(libexecdir)|g'


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


# systemd unit file
systemdsystemunitdir = $(SYSTEMD_UNIT_DIR)
systemdsystemunit_in_files = ModemManager.service.in
if HAVE_SYSTEMD
systemdsystemunit_DATA = ModemManager.service
ModemManager.service: ModemManager.service.in
	$(edit) $< >$@
endif


# DBus Activation file
dbusactivationdir = $(datadir)/dbus-1/system-services
dbusactivation_DATA = org.freedesktop.ModemManager1.service
dbusactivation_in_files = org.freedesktop.ModemManager1.service.in
org.freedesktop.ModemManager1.service: org.freedesktop.ModemManager1.service.in
	$(edit) $< >$@


# Icon
icondir=${datadir}/icons/hicolor/22x22/apps
icon_DATA = ModemManager.png


# Logos
logos = \
	ModemManager-logo-square.svg ModemManager-logo-square.png \
	ModemManager-logo-wide.svg ModemManager-logo-wide.png \
	ModemManager-logo-wide-text.svg ModemManager-logo-wide-text.png


# Diagrams
diagrams = \
	ModemManager-states.png \
	ModemManager-interface-initialization-sequence.png \
	ModemManager-interface-initialization-sequence-subclassed.png


# Polkit
polkit_policy_in_in_files = org.freedesktop.ModemManager1.policy.in.in
if WITH_POLKIT
polkit_policydir = $(datadir)/polkit-1/actions
polkit_policy_DATA = $(polkit_policy_in_in_files:.policy.in.in=.policy)
@INTLTOOL_POLICY_RULE@
endif


# Set up pkg-config .pc files for exported libraries
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = \
	ModemManager.pc \
	mm-glib.pc


DISTCLEANFILES = \
	$(dbusactivation_DATA) \
	$(dbusservice_DATA) \
	$(polkit_policy_DATA)

if HAVE_SYSTEMD
DISTCLEANFILES += $(systemdsystemunit_DATA)
endif


EXTRA_DIST = \
	$(systemdsystemunit_in_files) \
	$(dbusactivation_in_files) \
	$(dbusservice_file_polkit) \
	$(dbusservice_file_nopolkit) \
	$(icon_DATA) \
	$(polkit_policy_in_in_files) \
	$(logos) \
	$(diagrams)
