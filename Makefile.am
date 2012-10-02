
SUBDIRS = . build-aux data include libqcdm libwmc libmm-glib src plugins cli introspection uml290 po test docs

DISTCHECK_CONFIGURE_FLAGS = \
	--with-udev-base-dir="$$dc_install_base" \
	--with-tests=yes \
	--with-docs=yes

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
	$(INTLTOOL_FILES)

ACLOCAL_AMFLAGS = -I m4
