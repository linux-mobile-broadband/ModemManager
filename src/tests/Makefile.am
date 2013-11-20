include $(top_srcdir)/gtester.make

noinst_PROGRAMS = \
	test-modem-helpers \
	test-charsets \
	test-qcdm-serial-port \
	test-at-serial-port \
	test-sms-part-3gpp \
	test-sms-part-cdma

if WITH_QMI
noinst_PROGRAMS += test-modem-helpers-qmi
endif

TEST_PROGS += $(noinst_PROGRAMS)

################

test_modem_helpers_SOURCES = \
	test-modem-helpers.c

test_modem_helpers_CPPFLAGS = \
	$(MM_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include \
	-I$(top_srcdir)/libmm-glib \
	-I$(top_srcdir)/libmm-glib/generated \
	-I$(top_builddir)/libmm-glib/generated

test_modem_helpers_LDADD = \
	$(top_builddir)/src/libmodem-helpers.la \
	$(MM_LIBS)

if WITH_QMI
test_modem_helpers_CPPFLAGS += $(QMI_CFLAGS)
test_modem_helpers_LDADD += $(QMI_LIBS)
endif

################

if WITH_QMI
test_modem_helpers_qmi_SOURCES = \
	test-modem-helpers-qmi.c

test_modem_helpers_qmi_CPPFLAGS = \
	$(MM_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include \
	-I$(top_srcdir)/libmm-glib \
	-I$(top_srcdir)/libmm-glib/generated \
	-I$(top_builddir)/libmm-glib/generated \
	$(QMI_CFLAGS)

test_modem_helpers_qmi_LDADD = \
	$(top_builddir)/src/libmodem-helpers.la \
	$(MM_LIBS) \
	$(QMI_LIBS)
endif

################

test_charsets_SOURCES = \
	test-charsets.c

test_charsets_CPPFLAGS = \
	$(MM_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include \
	-I$(top_srcdir)/libmm-glib \
	-I$(top_srcdir)/libmm-glib/generated \
	-I$(top_builddir)/libmm-glib/generated

test_charsets_LDADD = \
	$(top_builddir)/src/libmodem-helpers.la \
	$(MM_LIBS)

if WITH_QMI
test_charsets_CPPFLAGS += $(QMI_CFLAGS)
test_charsets_LDADD += $(QMI_LIBS)
endif

################

test_qcdm_serial_port_SOURCES = \
	test-qcdm-serial-port.c

test_qcdm_serial_port_CPPFLAGS = \
	$(MM_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include \
	-I$(top_srcdir)/libmm-glib \
	-I$(top_srcdir)/libmm-glib/generated \
	-I$(top_builddir)/libmm-glib/generated

test_qcdm_serial_port_LDADD = \
	$(MM_LIBS) \
	$(top_builddir)/src/libport.la \
	$(top_builddir)/src/libmodem-helpers.la \
	$(top_builddir)/libqcdm/src/libqcdm.la \
	-lutil

if WITH_QMI
test_qcdm_serial_port_CPPFLAGS += $(QMI_CFLAGS)
test_qcdm_serial_port_LDADD += $(QMI_LIBS)
endif

################

test_at_serial_port_SOURCES = \
	test-at-serial-port.c

test_at_serial_port_CPPFLAGS = \
	$(MM_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include \
	-I$(top_srcdir)/libmm-glib \
	-I$(top_srcdir)/libmm-glib/generated \
	-I$(top_builddir)/libmm-glib/generated

test_at_serial_port_LDADD = \
	$(MM_LIBS) \
	$(top_builddir)/src/libport.la \
	$(top_builddir)/src/libmodem-helpers.la \
	-lutil

if WITH_QMI
test_at_serial_port_CPPFLAGS += $(QMI_CFLAGS)
test_at_serial_port_LDADD += $(QMI_LIBS)
endif

################

test_sms_part_3gpp_SOURCES = \
	test-sms-part-3gpp.c

test_sms_part_3gpp_CPPFLAGS = \
	$(MM_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include \
	-I$(top_srcdir)/libmm-glib \
	-I$(top_srcdir)/libmm-glib/generated \
	-I$(top_builddir)/libmm-glib/generated

test_sms_part_3gpp_LDADD = \
	$(top_builddir)/src/libmodem-helpers.la \
	$(MM_LIBS)

if WITH_QMI
test_sms_part_3gpp_CPPFLAGS += $(QMI_CFLAGS)
test_sms_part_3gpp_LDADD += $(QMI_LIBS)
endif

################

test_sms_part_cdma_SOURCES = \
	test-sms-part-cdma.c

test_sms_part_cdma_CPPFLAGS = \
	$(MM_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include \
	-I$(top_srcdir)/libmm-glib \
	-I$(top_srcdir)/libmm-glib/generated \
	-I$(top_builddir)/libmm-glib/generated

test_sms_part_cdma_LDADD = \
	$(top_builddir)/src/libmodem-helpers.la \
	$(MM_LIBS)

if WITH_QMI
test_sms_part_cdma_CPPFLAGS += $(QMI_CFLAGS)
test_sms_part_cdma_LDADD += $(QMI_LIBS)
endif
