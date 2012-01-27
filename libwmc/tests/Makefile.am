if WITH_TESTS

INCLUDES = \
	-I$(top_srcdir)/libwmc/src \
	-I$(top_srcdir)/src

noinst_PROGRAMS = test-wmc

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
	$(MM_CFLAGS)

test_wmc_LDADD = $(MM_LIBS)

if WMC_STANDALONE
test_wmc_LDADD += $(top_builddir)/src/libwmc.la
else
test_wmc_LDADD += $(top_builddir)/libwmc/src/libwmc.la
endif

check-local: test-wmc
	$(abs_builddir)/test-wmc

endif

