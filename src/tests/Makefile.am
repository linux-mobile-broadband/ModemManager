
include $(top_srcdir)/gtester.make

################################################################################
# common
################################################################################

AM_CFLAGS = \
	$(MM_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include \
	-I$(top_srcdir)/libqcdm/src \
	-I$(top_srcdir)/libmm-glib \
	-I${top_builddir}/libmm-glib/generated \
	-I${top_srcdir}/src/ \
	-I${top_builddir}/src/ \
	$(NULL)

AM_LDFLAGS = \
	$(MM_LIBS) \
	$(CODE_COVERAGE_LDFLAGS) \
	$(top_builddir)/src/libhelpers.la \
	$(top_builddir)/src/libport.la \
	-lutil \
	$(NULL)

if WITH_QMI
AM_CFLAGS  += $(QMI_CFLAGS)
AM_LDFLAGS += $(QMI_LIBS)
endif

if WITH_MBIM
AM_CFLAGS  += $(MBIM_CFLAGS)
AM_LDFLAGS += $(MBIM_LIBS)
endif

################################################################################
# tests
#  note: we abuse AM_LDFLAGS to include libhelpers and libport.
################################################################################

noinst_PROGRAMS = \
	test-modem-helpers \
	test-charsets \
	test-qcdm-serial-port \
	test-at-serial-port \
	test-sms-part-3gpp \
	test-sms-part-cdma \
	$(NULL)

if WITH_QMI
noinst_PROGRAMS += test-modem-helpers-qmi
endif

TEST_PROGS += $(noinst_PROGRAMS)
