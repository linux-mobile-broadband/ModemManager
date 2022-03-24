
# Directory for user-enabled tools
connectionuser = $(pkgsysconfdir)/connection.d

# Directory for package-enabled tools
connectionpackage = $(pkglibdir)/connection.d

# Shipped but disabled FCC unlock tools
connectionavailabledir = $(pkgdatadir)/connection.available.d
connectionavailable_SCRIPTS = \
	99-log-event \
	$(NULL)

EXTRA_DIST = $(connectionavailable_SCRIPTS)

install-data-hook:
	$(MKDIR_P) $(DESTDIR)$(connectionuser); \
	$(MKDIR_P) $(DESTDIR)$(connectionpackage); \
	cd $(DESTDIR)$(connectionavailabledir); \
	chmod go-rwx *; \
	$(NULL)
