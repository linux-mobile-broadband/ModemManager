include $(top_srcdir)/gtester.make

AM_CFLAGS = \
	$(WARN_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS) \
	$(NULL)

AM_LDFLAGS = \
	$(WARN_LDFLAGS) \
	$(CODE_COVERAGE_LDFLAGS) \
	$(NULL)

noinst_PROGRAMS = test-qcdm modepref ipv6pref reset
TEST_PROGS += test-qcdm

test_qcdm_SOURCES = \
	test-qcdm-crc.c \
	test-qcdm-crc.h \
	test-qcdm-escaping.c \
	test-qcdm-escaping.h \
	test-qcdm-utils.c \
	test-qcdm-utils.h \
	test-qcdm-com.c \
	test-qcdm-com.h \
	test-qcdm-result.c \
	test-qcdm-result.h \
	test-qcdm.c
test_qcdm_CPPFLAGS = \
	$(MM_CFLAGS) \
	-I$(top_srcdir)/libqcdm/src \
	-I$(top_srcdir)/src
test_qcdm_LDADD = $(MM_LIBS)

modepref_SOURCES = modepref.c
modepref_CPPFLAGS = \
	$(MM_CFLAGS) \
	-I$(top_srcdir)/libqcdm/src \
	-I$(top_srcdir)/src
modepref_LDADD = $(MM_LIBS)

ipv6pref_SOURCES = ipv6pref.c
ipv6pref_CPPFLAGS = \
	$(MM_CFLAGS) \
	-I$(top_srcdir)/libqcdm/src \
	-I$(top_srcdir)/src
ipv6pref_LDADD = $(MM_LIBS)

reset_SOURCES = reset.c
reset_CPPFLAGS = \
	$(MM_CFLAGS) \
	-I$(top_srcdir)/libqcdm/src \
	-I$(top_srcdir)/src
reset_LDADD = $(MM_LIBS)

if QCDM_STANDALONE
test_qcdm_LDADD += $(top_builddir)/src/libqcdm.la
modepref_LDADD += $(top_builddir)/src/libqcdm.la
ipv6pref_LDADD += $(top_builddir)/src/libqcdm.la
reset_LDADD +=  $(top_builddir)/src/libqcdm.la
else
test_qcdm_LDADD += $(top_builddir)/libqcdm/src/libqcdm.la
modepref_LDADD += $(top_builddir)/libqcdm/src/libqcdm.la
ipv6pref_LDADD += $(top_builddir)/libqcdm/src/libqcdm.la
reset_LDADD += $(top_builddir)/libqcdm/src/libqcdm.la
endif
