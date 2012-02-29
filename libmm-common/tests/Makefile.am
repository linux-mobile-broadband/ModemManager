
noinst_PROGRAMS = \
	test-common-helpers

test_common_helpers_SOURCES = \
	test-common-helpers.c

test_common_helpers_CPPFLAGS = \
	$(MM_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include \
	-I$(top_srcdir)/libmm-common \
	-I$(top_builddir)/libmm-common

test_common_helpers_LDADD = \
	$(top_builddir)/libmm-common/libmm-common.la \
	$(MM_LIBS)

if WITH_TESTS

check-local: test-common-helpers
	$(abs_builddir)/test-common-helpers

endif
