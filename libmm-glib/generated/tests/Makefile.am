AM_CFLAGS = $(CODE_COVERAGE_CFLAGS)
AM_LDFLAGS = $(CODE_COVERAGE_LDFLAGS)

noinst_LTLIBRARIES = libmm-test-generated.la

GENERATED_H = mm-gdbus-test.h
GENERATED_C = mm-gdbus-test.c

BUILT_SOURCES = $(GENERATED_H) $(GENERATED_C)

# Test interface
mm_gdbus_test_generated = \
	mm-gdbus-test.h \
	mm-gdbus-test.c
$(mm_gdbus_test_generated): $(top_srcdir)/introspection/tests/org.freedesktop.ModemManager1.Test.xml
	$(AM_V_GEN) $(GDBUS_CODEGEN) \
		--interface-prefix org.freedesktop.ModemManager1. \
		--c-namespace=MmGdbus \
		--generate-c-code mm-gdbus-test \
		$< \
		$(NULL)

nodist_libmm_test_generated_la_SOURCES = \
	$(GENERATED_H) \
	$(GENERATED_C)

libmm_test_generated_la_CPPFLAGS = \
	$(LIBMM_GLIB_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include \
	-Wno-unused-function \
	-Wno-float-equal \
	-Wno-shadow

libmm_test_generated_la_LIBADD = \
	$(LIBMM_GLIB_LIBS)

CLEANFILES = $(GENERATED_H) $(GENERATED_C)
