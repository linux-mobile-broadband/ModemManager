include $(top_srcdir)/gtester.make

################################################################################
# common
################################################################################

AM_CFLAGS = \
	$(WARN_CFLAGS) \
	$(MM_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include \
	-I$(top_srcdir)/libmm-glib \
	-I${top_srcdir}/libmm-glib/generated \
	-I${top_builddir}/libmm-glib/generated \
	$(NULL)

LDADD = \
	$(top_builddir)/libmm-glib/libmm-glib.la \
	$(NULL)

AM_LDFLAGS = \
	$(WARN_LDFLAGS) \
	$(MM_LIBS) \
	$(MM_LDFLAGS) \
	$(NULL)

noinst_PROGRAMS = test-stub
test_stub_CPPFLAGS = \
	-DTEST_SERVICES=\""$(abs_top_builddir)/tools/tests/services"\" \
	$(NULL)

# only run the test if introspection was enabled
if HAVE_INTROSPECTION
TEST_PROGS += $(noinst_PROGRAMS)
endif

test-wrapper.sh: test-wrapper.sh.in
	@sed \
	-e 's|@abs_top_builddir[@]|$(abs_top_builddir)|g' \
	-e 's|@abs_top_srcdir[@]|$(abs_top_srcdir)|g' \
	$< >$@
	@chmod +x $@

BUILT_SOURCES = test-wrapper.sh
CLEANFILES = test-wrapper.sh

EXTRA_DIST += test-wrapper.sh.in services/org.freedesktop.ModemManager1.service.in
