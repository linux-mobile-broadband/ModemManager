include $(top_srcdir)/gtester.make

AM_CFLAGS = $(CODE_COVERAGE_CFLAGS)
AM_LDFLAGS = $(CODE_COVERAGE_LDFLAGS)

noinst_PROGRAMS = test-common-helpers
TEST_PROGS += $(noinst_PROGRAMS)

test_common_helpers_SOURCES = \
	test-common-helpers.c

test_common_helpers_CPPFLAGS = \
	$(MM_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include \
	-I$(top_srcdir)/libmm-glib \
	-I$(top_builddir)/libmm-glib \
	-I${top_srcdir}/libmm-glib/generated \
	-I${top_builddir}/libmm-glib/generated \
	-DLIBMM_GLIB_COMPILATION

test_common_helpers_LDADD = \
	$(top_builddir)/libmm-glib/libmm-glib.la \
	$(MM_LIBS)
