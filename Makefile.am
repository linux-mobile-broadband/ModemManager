
SUBDIRS = \
	. \
	build-aux \
	po \
	data \
	include \
	libqcdm \
	libmm-glib \
	src \
	plugins \
	cli \
	vapi \
	introspection \
	test \
	tools \
	examples \
	docs \
	$(NULL)

ChangeLog:
	$(AM_V_GEN) if test -d "$(srcdir)/.git"; then \
	  (GIT_DIR=$(top_srcdir)/.git $(top_srcdir)/missing --run git log --stat) | fmt --split-only > $@.tmp \
	  && mv -f $@.tmp $@ \
	  || ($(RM) $@.tmp; \
	      echo Failed to generate ChangeLog, your ChangeLog may be outdated >&2; \
	      (test -f $@ || echo git-log is required to generate this file >> $@)); \
	else \
	  test -f $@ || \
	  (echo A git checkout and git-log is required to generate ChangeLog >&2 && \
	  echo A git checkout and git-log is required to generate this file >> $@); \
	fi

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
