INCLUDES = \
	-I$(top_srcdir)/src

noinst_PROGRAMS = \
	test-modem-helpers \
	test-charsets \
	test-qcdm-serial-port \
	test-sms

test_modem_helpers_SOURCES = \
	test-modem-helpers.c

test_modem_helpers_CPPFLAGS = \
	$(MM_CFLAGS)

test_modem_helpers_LDADD = \
	$(top_builddir)/src/libmodem-helpers.la \
	$(MM_LIBS)

test_charsets_SOURCES = \
	test-charsets.c

test_charsets_CPPFLAGS = \
	$(MM_CFLAGS)

test_charsets_LDADD = \
	$(top_builddir)/src/libmodem-helpers.la \
	$(MM_LIBS)

test_qcdm_serial_port_SOURCES = \
	test-qcdm-serial-port.c

test_qcdm_serial_port_CPPFLAGS = \
	$(MM_CFLAGS) \
	-I$(top_srcdir)

test_qcdm_serial_port_LDADD = \
	$(MM_LIBS) \
	$(top_builddir)/src/libserial.la \
	$(top_builddir)/src/libmodem-helpers.la \
	$(top_builddir)/libqcdm/src/libqcdm.la \
	-lutil

test_sms_SOURCES = \
	test-sms.c

test_sms_CFLAGS = \
	$(MM_CFLAGS)

test_sms_LDADD = \
	$(top_builddir)/src/libmodem-helpers.la \
	$(MM_LIBS)

if WITH_TESTS

check-local: test-modem-helpers
	$(abs_builddir)/test-modem-helpers
	$(abs_builddir)/test-charsets
	$(abs_builddir)/test-qcdm-serial-port
	$(abs_builddir)/test-sms

endif

