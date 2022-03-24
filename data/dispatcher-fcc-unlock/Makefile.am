
# Directory for user-enabled tools
fccunlockuser = $(pkgsysconfdir)/fcc-unlock.d

# Directory for package-enabled tools
fccunlockpackage = $(pkglibdir)/fcc-unlock.d

# Shipped but disabled FCC unlock tools
fccunlockavailabledir = $(pkgdatadir)/fcc-unlock.available.d
fccunlockavailable_SCRIPTS = \
	105b \
	1199 \
	1eac \
	$(NULL)

EXTRA_DIST = $(fccunlockavailable_SCRIPTS)

install-data-hook:
	$(MKDIR_P) $(DESTDIR)$(fccunlockuser); \
	$(MKDIR_P) $(DESTDIR)$(fccunlockpackage); \
	cd $(DESTDIR)$(fccunlockavailabledir); \
	chmod go-rwx *; \
	$(LN_S) -f 105b 105b:e0ab; \
	$(LN_S) -f 1199 03f0:4e1d; \
	$(LN_S) -f 1199 1199:9079; \
	$(LN_S) -f 1199 413c:81a3; \
	$(LN_S) -f 1199 413c:81a8; \
	$(LN_S) -f 1eac 1eac:1001; \
	$(NULL)

uninstall-hook:
	cd $(DESTDIR)$(fccunlockavailabledir) && rm -f *:*
