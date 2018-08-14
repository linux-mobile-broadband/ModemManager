include $(top_srcdir)/gtester.make

AM_CFLAGS = $(CODE_COVERAGE_CFLAGS)
AM_LDFLAGS = $(CODE_COVERAGE_LDFLAGS)

LIBMM_GLIB_TESTS_COMMON_CPPFLAGS = \
	$(MM_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include \
	-I$(top_srcdir)/libmm-glib \
	-I$(top_builddir)/libmm-glib \
	-I${top_srcdir}/libmm-glib/generated \
	-I${top_builddir}/libmm-glib/generated \
	-DLIBMM_GLIB_COMPILATION

LIBMM_GLIB_TESTS_COMMON_LDADD = \
	$(top_builddir)/libmm-glib/libmm-glib.la \
	$(MM_LIBS)

noinst_PROGRAMS = \
	test-common-helpers \
	test-pco
TEST_PROGS += $(noinst_PROGRAMS)

test_common_helpers_SOURCES = test-common-helpers.c
test_common_helpers_CPPFLAGS = $(LIBMM_GLIB_TESTS_COMMON_CPPFLAGS)
test_common_helpers_LDADD = $(LIBMM_GLIB_TESTS_COMMON_LDADD)

test_pco_SOURCES = test-pco.c
test_pco_CPPFLAGS = $(LIBMM_GLIB_TESTS_COMMON_CPPFLAGS)
test_pco_LDADD = $(LIBMM_GLIB_TESTS_COMMON_LDADD)
