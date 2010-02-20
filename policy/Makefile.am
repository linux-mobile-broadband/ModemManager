icondir=${datadir}/icons/hicolor/22x22/apps
icon_DATA = modem-manager.png

polkit_policydir = $(datadir)/polkit-1/actions

dist_polkit_policy_in_files = org.freedesktop.modem-manager.policy.in
dist_polkit_policy_DATA = $(dist_polkit_policy_in_files:.policy.in=.policy)

@INTLTOOL_POLICY_RULE@

clean-local :
	rm -f *~

EXTRA_DIST = $(dist_polkit_policy_in_files) $(icon_DATA)

