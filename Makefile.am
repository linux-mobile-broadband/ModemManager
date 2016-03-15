
SUBDIRS = \
	. \
	build-aux \
	data \
	include \
	libqcdm \
	libwmc \
	libmm-glib \
	src \
	plugins \
	cli \
	vapi \
	introspection \
	uml290 \
	po \
	test \
	examples \
	docs \
	$(NULL)

DISTCHECK_CONFIGURE_FLAGS = \
	--with-udev-base-dir="$$dc_install_base" \
	--with-systemdsystemunitdir="$$dc_install_base/$(SYSTEMD_UNIT_DIR)" \
	--enable-gtk-doc=yes \
	$(NULL)

INTLTOOL_FILES = \
	intltool-extract.in \
	intltool-merge.in \
	intltool-update.in \
	$(NULL)

DISTCLEANFILES = \
	intltool-extract \
	intltool-merge \
	intltool-update \
	po/.intltool-merge-cache \
	$(NULL)

EXTRA_DIST = \
	$(INTLTOOL_FILES) \
	autogen.sh \
	gtester.make \
	COPYING.LIB \
	$(NULL)

ACLOCAL_AMFLAGS = -I m4

@CODE_COVERAGE_RULES@

if CODE_COVERAGE_ENABLED
clean-local:
	-find $(top_builddir) -name "*.gcno" -delete
endif
