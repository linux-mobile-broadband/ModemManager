INCLUDES = \
	-I$(top_srcdir)/src

noinst_PROGRAMS = test-modem-helpers test-charsets

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

if WITH_TESTS

check-local: test-modem-helpers
	$(abs_builddir)/test-modem-helpers
	$(abs_builddir)/test-charsets

endif

