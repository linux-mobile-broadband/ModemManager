
SUBDIRS = \
	. \
	build-aux \
	po \
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
	test \
	examples \
	docs \
	$(NULL)

AM_DISTCHECK_CONFIGURE_FLAGS = \
	--with-udev-base-dir="$$dc_install_base" \
	--with-systemdsystemunitdir="$$dc_install_base/$(SYSTEMD_UNIT_DIR)" \
	--enable-gtk-doc=yes \
	$(NULL)

EXTRA_DIST = \
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
