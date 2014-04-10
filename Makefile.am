
SUBDIRS = . build-aux data include libqcdm libwmc libmm-glib src plugins cli vapi introspection uml290 po test examples docs

DISTCHECK_CONFIGURE_FLAGS = \
	--with-udev-base-dir="$$dc_install_base" \
	--with-systemdsystemunitdir="$$dc_install_base/$(SYSTEMD_UNIT_DIR)" \
	--enable-gtk-doc=yes

INTLTOOL_FILES = \
	intltool-extract.in \
	intltool-merge.in \
	intltool-update.in

DISTCLEANFILES = \
	intltool-extract \
	intltool-merge \
	intltool-update \
	po/.intltool-merge-cache

EXTRA_DIST = \
	$(INTLTOOL_FILES) \
	autogen.sh \
	gtester.make \
	COPYING.LIB

ACLOCAL_AMFLAGS = -I m4
