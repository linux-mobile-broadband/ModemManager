include $(top_srcdir)/gtester.make

AM_CFLAGS = $(CODE_COVERAGE_CFLAGS)
AM_LDFLAGS = $(CODE_COVERAGE_LDFLAGS)

noinst_PROGRAMS = test-wmc
TEST_PROGS += $(noinst_PROGRAMS)

test_wmc_SOURCES = \
	test-wmc-crc.c \
	test-wmc-crc.h \
	test-wmc-escaping.c \
	test-wmc-escaping.h \
	test-wmc-utils.c \
	test-wmc-utils.h \
	test-wmc-com.c \
	test-wmc-com.h \
	test-wmc.c

test_wmc_CPPFLAGS = \
	$(MM_CFLAGS) \
	-I$(top_srcdir)/libwmc/src \
	-I$(top_srcdir)/src

test_wmc_LDADD = $(MM_LIBS)

if WMC_STANDALONE
test_wmc_LDADD += $(top_builddir)/src/libwmc.la
else
test_wmc_LDADD += $(top_builddir)/libwmc/src/libwmc.la
endif
