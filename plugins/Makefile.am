
include $(top_srcdir)/gtester.make

# Common CPPFLAGS and LDFLAGS

PLUGIN_COMMON_COMPILER_FLAGS = \
	$(MM_CFLAGS) \
	$(GUDEV_CFLAGS)	\
	-I$(top_srcdir) \
	-I$(top_srcdir)/src \
	-I$(top_builddir)/src \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include \
	-I$(top_srcdir)/libmm-glib \
	-I$(top_srcdir)/libmm-glib/generated \
	-I$(top_builddir)/libmm-glib/generated

PLUGIN_COMMON_LINKER_FLAGS = \
	$(GUDEV_LIBS) \
	$(MM_LIBS) \
	-module \
	-avoid-version

if WITH_QMI
PLUGIN_COMMON_COMPILER_FLAGS += $(QMI_CFLAGS)
PLUGIN_COMMON_LINKER_FLAGS += $(QMI_LIBS)
endif

if WITH_MBIM
PLUGIN_COMMON_COMPILER_FLAGS += $(MBIM_CFLAGS)
PLUGIN_COMMON_LINKER_FLAGS += $(MBIM_LIBS)
endif

# UDev rules
udevrulesdir = $(UDEV_BASE_DIR)/rules.d
udevrules_DATA =

# Unit tests
noinst_PROGRAMS =

# Helper libs
noinst_LTLIBRARIES =

########################################

# Common service test support

noinst_LTLIBRARIES += libmm-test-common.la

libmm_test_common_la_SOURCES = \
	tests/test-fixture.h \
	tests/test-fixture.c \
	tests/test-port-context.h \
	tests/test-port-context.c

libmm_test_common_la_CPPFLAGS = \
	$(MM_CFLAGS) \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include \
	-I$(top_srcdir)/libmm-glib \
	-I$(top_srcdir)/libmm-glib/generated \
	-I$(top_builddir)/libmm-glib/generated \
	-I$(top_builddir)/libmm-glib/generated/tests \
	-DTEST_SERVICES=\""$(abs_top_builddir)/data/tests"\"

libmm_test_common_la_LIBADD = \
	${top_builddir}/libmm-glib/generated/tests/libmm-test-generated.la \
	$(top_builddir)/libmm-glib/libmm-glib.la

TEST_COMMON_COMPILER_FLAGS = \
	$(MM_CFLAGS) \
	-I$(top_srcdir)/plugins/tests \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include \
	-I$(top_srcdir)/libmm-glib \
	-I$(top_srcdir)/libmm-glib/generated \
	-I$(top_builddir)/libmm-glib/generated \
	-I$(top_builddir)/libmm-glib/generated/tests \
	-DCOMMON_GSM_PORT_CONF=\""$(abs_top_srcdir)/plugins/tests/gsm-port.conf"\"

TEST_COMMON_LIBADD_FLAGS = \
	$(builddir)/libmm-test-common.la \
	$(top_builddir)/libmm-glib/libmm-glib.la


########################################

# Icera-specific support
noinst_LTLIBRARIES += libmm-utils-icera.la
libmm_utils_icera_la_SOURCES = \
	icera/mm-broadband-modem-icera.h \
	icera/mm-broadband-modem-icera.c \
	icera/mm-broadband-bearer-icera.h \
	icera/mm-broadband-bearer-icera.c \
	icera/mm-modem-helpers-icera.c \
	icera/mm-modem-helpers-icera.h
libmm_utils_icera_la_CPPFLAGS = $(PLUGIN_COMMON_COMPILER_FLAGS)
libmm_utils_icera_la_LIBADD = $(GUDEV_LIBS) $(MM_LIBS)

ICERA_COMMON_COMPILER_FLAGS = -I$(top_srcdir)/plugins/icera
ICERA_COMMON_LIBADD_FLAGS = $(builddir)/libmm-utils-icera.la

noinst_PROGRAMS += test-modem-helpers-icera
test_modem_helpers_icera_SOURCES = \
	icera/mm-modem-helpers-icera.c \
	icera/mm-modem-helpers-icera.h \
	icera/tests/test-modem-helpers-icera.c
test_modem_helpers_icera_CPPFLAGS = \
	-I$(top_srcdir)/plugins/icera \
	$(PLUGIN_COMMON_COMPILER_FLAGS)
test_modem_helpers_icera_LDADD = \
       $(top_builddir)/libmm-glib/libmm-glib.la \
       $(top_builddir)/src/libmodem-helpers.la
test_modem_helpers_icera_LDFLAGS = $(PLUGIN_COMMON_LINKER_FLAGS)

########################################

pkglib_LTLIBRARIES = \
	libmm-plugin-generic.la \
	libmm-plugin-nokia.la \
	libmm-plugin-nokia-icera.la \
	libmm-plugin-cinterion.la \
	libmm-plugin-iridium.la \
	libmm-plugin-gobi.la \
	libmm-plugin-motorola.la \
	libmm-plugin-novatel.la \
	libmm-plugin-novatel-lte.la \
	libmm-plugin-altair-lte.la \
	libmm-plugin-samsung.la \
	libmm-plugin-option.la \
	libmm-plugin-hso.la \
	libmm-plugin-anydata.la \
	libmm-plugin-linktop.la \
	libmm-plugin-simtech.la \
	libmm-plugin-wavecom.la \
	libmm-plugin-huawei.la \
	libmm-plugin-longcheer.la \
	libmm-plugin-x22x.la \
	libmm-plugin-pantech.la \
	libmm-plugin-zte.la \
	libmm-plugin-sierra.la \
	libmm-plugin-mbm.la \
	libmm-plugin-via.la \
	libmm-plugin-telit.la \
	libmm-plugin-mtk.la

# Generic
libmm_plugin_generic_la_SOURCES = \
	generic/mm-plugin-generic.c \
	generic/mm-plugin-generic.h
libmm_plugin_generic_la_CPPFLAGS = $(PLUGIN_COMMON_COMPILER_FLAGS)
libmm_plugin_generic_la_LDFLAGS = $(PLUGIN_COMMON_LINKER_FLAGS)

noinst_PROGRAMS += test-service-generic
test_service_generic_SOURCES = generic/tests/test-service-generic.c
test_service_generic_CPPFLAGS = $(TEST_COMMON_COMPILER_FLAGS)
test_service_generic_LDADD = $(TEST_COMMON_LIBADD_FLAGS)
test_service_generic_LDFLAGS = $(PLUGIN_COMMON_LINKER_FLAGS)

## Motorola
libmm_plugin_motorola_la_SOURCES = \
	motorola/mm-plugin-motorola.c \
	motorola/mm-plugin-motorola.h \
	motorola/mm-broadband-modem-motorola.c \
	motorola/mm-broadband-modem-motorola.h
libmm_plugin_motorola_la_CPPFLAGS = $(PLUGIN_COMMON_COMPILER_FLAGS)
libmm_plugin_motorola_la_LDFLAGS = $(PLUGIN_COMMON_LINKER_FLAGS)

# Gobi
libmm_plugin_gobi_la_SOURCES = \
	gobi/mm-plugin-gobi.c \
	gobi/mm-plugin-gobi.h \
	gobi/mm-broadband-modem-gobi.c \
	gobi/mm-broadband-modem-gobi.h
libmm_plugin_gobi_la_CPPFLAGS = $(PLUGIN_COMMON_COMPILER_FLAGS)
libmm_plugin_gobi_la_LDFLAGS = $(PLUGIN_COMMON_LINKER_FLAGS)

# Huawei
libmm_plugin_huawei_la_SOURCES = \
	huawei/mm-plugin-huawei.c \
	huawei/mm-plugin-huawei.h \
	huawei/mm-modem-helpers-huawei.c \
	huawei/mm-modem-helpers-huawei.h \
	huawei/mm-sim-huawei.c \
	huawei/mm-sim-huawei.h \
	huawei/mm-broadband-modem-huawei.c \
	huawei/mm-broadband-modem-huawei.h \
	huawei/mm-broadband-bearer-huawei.c \
	huawei/mm-broadband-bearer-huawei.h
libmm_plugin_huawei_la_CPPFLAGS = $(PLUGIN_COMMON_COMPILER_FLAGS)
libmm_plugin_huawei_la_LDFLAGS = $(PLUGIN_COMMON_LINKER_FLAGS)

udevrules_DATA += huawei/77-mm-huawei-net-port-types.rules

noinst_PROGRAMS += test-modem-helpers-huawei
test_modem_helpers_huawei_SOURCES = \
	huawei/mm-modem-helpers-huawei.c \
	huawei/mm-modem-helpers-huawei.h \
	huawei/tests/test-modem-helpers-huawei.c
test_modem_helpers_huawei_CPPFLAGS = \
	-I$(top_srcdir)/plugins/huawei \
	$(PLUGIN_COMMON_COMPILER_FLAGS)
test_modem_helpers_huawei_LDADD = \
       $(top_builddir)/libmm-glib/libmm-glib.la \
       $(top_builddir)/src/libmodem-helpers.la
test_modem_helpers_huawei_LDFLAGS = $(PLUGIN_COMMON_LINKER_FLAGS)

# MBM
libmm_plugin_mbm_la_SOURCES = \
	mbm/mm-plugin-mbm.c \
	mbm/mm-plugin-mbm.h \
	mbm/mm-broadband-modem-mbm.c \
	mbm/mm-broadband-modem-mbm.h \
	mbm/mm-broadband-bearer-mbm.c \
	mbm/mm-broadband-bearer-mbm.h \
	mbm/mm-modem-helpers-mbm.c \
	mbm/mm-modem-helpers-mbm.h \
	mbm/mm-sim-mbm.c \
	mbm/mm-sim-mbm.h
libmm_plugin_mbm_la_CPPFLAGS = $(PLUGIN_COMMON_COMPILER_FLAGS)
libmm_plugin_mbm_la_LDFLAGS = $(PLUGIN_COMMON_LINKER_FLAGS)
udevrules_DATA += mbm/77-mm-ericsson-mbm.rules

noinst_PROGRAMS += test-modem-helpers-mbm
test_modem_helpers_mbm_SOURCES = \
	mbm/mm-modem-helpers-mbm.c \
	mbm/mm-modem-helpers-mbm.h \
	mbm/tests/test-modem-helpers-mbm.c
test_modem_helpers_mbm_CPPFLAGS = \
	-I$(top_srcdir)/plugins/mbm \
	$(PLUGIN_COMMON_COMPILER_FLAGS)
test_modem_helpers_mbm_LDADD = \
       $(top_builddir)/libmm-glib/libmm-glib.la \
       $(top_builddir)/src/libmodem-helpers.la
test_modem_helpers_mbm_LDFLAGS = $(PLUGIN_COMMON_LINKER_FLAGS)

# Option
libmm_plugin_option_la_SOURCES = \
	option/mm-plugin-option.c \
	option/mm-plugin-option.h \
	option/mm-broadband-modem-option.c \
	option/mm-broadband-modem-option.h
libmm_plugin_option_la_CPPFLAGS = $(PLUGIN_COMMON_COMPILER_FLAGS)
libmm_plugin_option_la_LDFLAGS = $(PLUGIN_COMMON_LINKER_FLAGS)

# HSO
libmm_plugin_hso_la_SOURCES = \
	option/mm-plugin-hso.c \
	option/mm-plugin-hso.h \
	option/mm-broadband-modem-option.c \
	option/mm-broadband-modem-option.h \
	option/mm-broadband-bearer-hso.c \
	option/mm-broadband-bearer-hso.h \
	option/mm-broadband-modem-hso.c \
	option/mm-broadband-modem-hso.h
libmm_plugin_hso_la_CPPFLAGS = $(PLUGIN_COMMON_COMPILER_FLAGS)
libmm_plugin_hso_la_LDFLAGS = $(PLUGIN_COMMON_LINKER_FLAGS)

# Sierra
libmm_plugin_sierra_la_SOURCES = \
	sierra/mm-plugin-sierra.c \
	sierra/mm-plugin-sierra.h \
	sierra/mm-common-sierra.c \
	sierra/mm-common-sierra.h \
	sierra/mm-sim-sierra.c \
	sierra/mm-sim-sierra.h \
	sierra/mm-broadband-bearer-sierra.c \
	sierra/mm-broadband-bearer-sierra.h \
	sierra/mm-broadband-modem-sierra.c \
	sierra/mm-broadband-modem-sierra.h \
	sierra/mm-broadband-modem-sierra-icera.c \
	sierra/mm-broadband-modem-sierra-icera.h
libmm_plugin_sierra_la_CPPFLAGS = $(PLUGIN_COMMON_COMPILER_FLAGS) $(ICERA_COMMON_COMPILER_FLAGS)
libmm_plugin_sierra_la_LDFLAGS = $(PLUGIN_COMMON_LINKER_FLAGS)
libmm_plugin_sierra_la_LIBADD = $(ICERA_COMMON_LIBADD_FLAGS)

# Wavecom (Sierra Airlink)
libmm_plugin_wavecom_la_SOURCES = \
	wavecom/mm-plugin-wavecom.c \
	wavecom/mm-plugin-wavecom.h \
	wavecom/mm-broadband-modem-wavecom.c \
	wavecom/mm-broadband-modem-wavecom.h
libmm_plugin_wavecom_la_CPPFLAGS = $(PLUGIN_COMMON_COMPILER_FLAGS)
libmm_plugin_wavecom_la_LDFLAGS = $(PLUGIN_COMMON_LINKER_FLAGS)

# Nokia
libmm_plugin_nokia_la_SOURCES = \
	nokia/mm-plugin-nokia.c \
	nokia/mm-plugin-nokia.h \
	nokia/mm-sim-nokia.c \
	nokia/mm-sim-nokia.h \
	nokia/mm-broadband-modem-nokia.c \
	nokia/mm-broadband-modem-nokia.h
libmm_plugin_nokia_la_CPPFLAGS = $(PLUGIN_COMMON_COMPILER_FLAGS)
libmm_plugin_nokia_la_LDFLAGS = $(PLUGIN_COMMON_LINKER_FLAGS)

# Nokia (Icera)
libmm_plugin_nokia_icera_la_SOURCES = \
	nokia/mm-plugin-nokia-icera.c \
	nokia/mm-plugin-nokia-icera.h
libmm_plugin_nokia_icera_la_CPPFLAGS = $(PLUGIN_COMMON_COMPILER_FLAGS) $(ICERA_COMMON_COMPILER_FLAGS)
libmm_plugin_nokia_icera_la_LDFLAGS = $(PLUGIN_COMMON_LINKER_FLAGS)
libmm_plugin_nokia_icera_la_LIBADD = $(ICERA_COMMON_LIBADD_FLAGS)
udevrules_DATA += nokia/77-mm-nokia-port-types.rules

# Zte
libmm_plugin_zte_la_SOURCES = \
	zte/mm-plugin-zte.c \
	zte/mm-plugin-zte.h \
	zte/mm-common-zte.h \
	zte/mm-common-zte.c \
	zte/mm-broadband-modem-zte.h \
	zte/mm-broadband-modem-zte.c \
	zte/mm-broadband-modem-zte-icera.h \
	zte/mm-broadband-modem-zte-icera.c
libmm_plugin_zte_la_CPPFLAGS = $(PLUGIN_COMMON_COMPILER_FLAGS) $(ICERA_COMMON_COMPILER_FLAGS)
libmm_plugin_zte_la_LDFLAGS = $(PLUGIN_COMMON_LINKER_FLAGS)
libmm_plugin_zte_la_LIBADD = $(ICERA_COMMON_LIBADD_FLAGS)
udevrules_DATA += zte/77-mm-zte-port-types.rules

# Longcheer (and rebranded dongles)
libmm_plugin_longcheer_la_SOURCES = \
	longcheer/mm-plugin-longcheer.c \
	longcheer/mm-plugin-longcheer.h \
	longcheer/mm-broadband-modem-longcheer.h \
	longcheer/mm-broadband-modem-longcheer.c
libmm_plugin_longcheer_la_CPPFLAGS = $(PLUGIN_COMMON_COMPILER_FLAGS)
libmm_plugin_longcheer_la_LDFLAGS = $(PLUGIN_COMMON_LINKER_FLAGS)
udevrules_DATA += longcheer/77-mm-longcheer-port-types.rules

# AnyData CDMA
libmm_plugin_anydata_la_SOURCES = \
	anydata/mm-plugin-anydata.c \
	anydata/mm-plugin-anydata.h \
	anydata/mm-broadband-modem-anydata.h \
	anydata/mm-broadband-modem-anydata.c
libmm_plugin_anydata_la_CPPFLAGS = $(PLUGIN_COMMON_COMPILER_FLAGS)
libmm_plugin_anydata_la_LDFLAGS = $(PLUGIN_COMMON_LINKER_FLAGS)

# Linktop CDMA
libmm_plugin_linktop_la_SOURCES = \
	linktop/mm-plugin-linktop.c \
	linktop/mm-plugin-linktop.h \
	linktop/mm-broadband-modem-linktop.h \
	linktop/mm-broadband-modem-linktop.c
libmm_plugin_linktop_la_CPPFLAGS = $(PLUGIN_COMMON_COMPILER_FLAGS)
libmm_plugin_linktop_la_LDFLAGS = $(PLUGIN_COMMON_LINKER_FLAGS)

# SimTech
libmm_plugin_simtech_la_SOURCES = \
	simtech/mm-plugin-simtech.c \
	simtech/mm-plugin-simtech.h \
	simtech/mm-broadband-modem-simtech.h \
	simtech/mm-broadband-modem-simtech.c
libmm_plugin_simtech_la_CPPFLAGS = $(PLUGIN_COMMON_COMPILER_FLAGS)
libmm_plugin_simtech_la_LDFLAGS = $(PLUGIN_COMMON_LINKER_FLAGS)
udevrules_DATA += simtech/77-mm-simtech-port-types.rules

# Alcatel/TCT/JRD x220D and possibly others
libmm_plugin_x22x_la_SOURCES = \
	x22x/mm-plugin-x22x.c \
	x22x/mm-plugin-x22x.h \
	x22x/mm-broadband-modem-x22x.h \
	x22x/mm-broadband-modem-x22x.c
libmm_plugin_x22x_la_CPPFLAGS = $(PLUGIN_COMMON_COMPILER_FLAGS)
libmm_plugin_x22x_la_LDFLAGS = $(PLUGIN_COMMON_LINKER_FLAGS)
udevrules_DATA += x22x/77-mm-x22x-port-types.rules

# Pantech
libmm_plugin_pantech_la_SOURCES = \
	pantech/mm-plugin-pantech.c \
	pantech/mm-plugin-pantech.h \
	pantech/mm-sim-pantech.c \
	pantech/mm-sim-pantech.h \
	pantech/mm-broadband-modem-pantech.c \
	pantech/mm-broadband-modem-pantech.h
libmm_plugin_pantech_la_CPPFLAGS = $(PLUGIN_COMMON_COMPILER_FLAGS)
libmm_plugin_pantech_la_LDFLAGS = $(PLUGIN_COMMON_LINKER_FLAGS)

# Samsung modem
libmm_plugin_samsung_la_SOURCES = \
	samsung/mm-plugin-samsung.c \
	samsung/mm-plugin-samsung.h \
	samsung/mm-broadband-modem-samsung.c \
	samsung/mm-broadband-modem-samsung.h
libmm_plugin_samsung_la_CPPFLAGS = $(PLUGIN_COMMON_COMPILER_FLAGS) $(ICERA_COMMON_COMPILER_FLAGS)
libmm_plugin_samsung_la_LDFLAGS = $(PLUGIN_COMMON_LINKER_FLAGS)
libmm_plugin_samsung_la_LIBADD = $(ICERA_COMMON_LIBADD_FLAGS)

# Cinterion (previously Siemens) modem
libmm_plugin_cinterion_la_SOURCES = \
	cinterion/mm-plugin-cinterion.c \
	cinterion/mm-plugin-cinterion.h \
	cinterion/mm-modem-helpers-cinterion.c \
	cinterion/mm-modem-helpers-cinterion.h \
	cinterion/mm-broadband-modem-cinterion.c \
	cinterion/mm-broadband-modem-cinterion.h
libmm_plugin_cinterion_la_CPPFLAGS = $(PLUGIN_COMMON_COMPILER_FLAGS)
libmm_plugin_cinterion_la_LDFLAGS = $(PLUGIN_COMMON_LINKER_FLAGS)

noinst_PROGRAMS += test-modem-helpers-cinterion
test_modem_helpers_cinterion_SOURCES = \
	cinterion/mm-modem-helpers-cinterion.c \
	cinterion/mm-modem-helpers-cinterion.h \
	cinterion/tests/test-modem-helpers-cinterion.c
test_modem_helpers_cinterion_CPPFLAGS = \
	-I$(top_srcdir)/plugins/cinterion \
	$(PLUGIN_COMMON_COMPILER_FLAGS)
test_modem_helpers_cinterion_LDADD = \
       $(top_builddir)/libmm-glib/libmm-glib.la \
       $(top_builddir)/src/libmodem-helpers.la
test_modem_helpers_cinterion_LDFLAGS = $(PLUGIN_COMMON_LINKER_FLAGS)

# Iridium modem
libmm_plugin_iridium_la_SOURCES = \
	iridium/mm-plugin-iridium.c \
	iridium/mm-plugin-iridium.h \
	iridium/mm-broadband-modem-iridium.c \
	iridium/mm-broadband-modem-iridium.h \
	iridium/mm-bearer-iridium.c \
	iridium/mm-bearer-iridium.h \
	iridium/mm-sim-iridium.c \
	iridium/mm-sim-iridium.h
libmm_plugin_iridium_la_CPPFLAGS = $(PLUGIN_COMMON_COMPILER_FLAGS)
libmm_plugin_iridium_la_LDFLAGS = $(PLUGIN_COMMON_LINKER_FLAGS)

# Novatel LTE modem
libmm_plugin_novatel_lte_la_SOURCES = \
	novatel/mm-plugin-novatel-lte.c \
	novatel/mm-plugin-novatel-lte.h \
	novatel/mm-broadband-modem-novatel-lte.c \
	novatel/mm-broadband-modem-novatel-lte.h \
	novatel/mm-broadband-bearer-novatel-lte.c \
	novatel/mm-broadband-bearer-novatel-lte.h \
	novatel/mm-sim-novatel-lte.c \
	novatel/mm-sim-novatel-lte.h
libmm_plugin_novatel_lte_la_CPPFLAGS = $(PLUGIN_COMMON_COMPILER_FLAGS)
libmm_plugin_novatel_lte_la_LDFLAGS = $(PLUGIN_COMMON_LINKER_FLAGS)

# Novatel non-LTE modem
libmm_plugin_novatel_la_SOURCES = \
	novatel/mm-plugin-novatel.c \
	novatel/mm-plugin-novatel.h \
	novatel/mm-broadband-modem-novatel.c \
	novatel/mm-broadband-modem-novatel.h
libmm_plugin_novatel_la_CPPFLAGS = $(PLUGIN_COMMON_COMPILER_FLAGS)
libmm_plugin_novatel_la_LDFLAGS = $(PLUGIN_COMMON_LINKER_FLAGS)

# Altair LTE modem
libmm_plugin_altair_lte_la_SOURCES = \
	altair/mm-modem-helpers-altair-lte.c \
	altair/mm-modem-helpers-altair-lte.h \
	altair/mm-plugin-altair-lte.c \
	altair/mm-plugin-altair-lte.h \
	altair/mm-broadband-modem-altair-lte.c \
	altair/mm-broadband-modem-altair-lte.h \
	altair/mm-broadband-bearer-altair-lte.c \
	altair/mm-broadband-bearer-altair-lte.h
libmm_plugin_altair_lte_la_CPPFLAGS = $(PLUGIN_COMMON_COMPILER_FLAGS)
libmm_plugin_altair_lte_la_LDFLAGS = $(PLUGIN_COMMON_LINKER_FLAGS)

noinst_PROGRAMS += test-modem-helpers-altair-lte
test_modem_helpers_altair_lte_SOURCES = \
	altair/mm-modem-helpers-altair-lte.c \
	altair/mm-modem-helpers-altair-lte.h \
	altair/tests/test-modem-helpers-altair-lte.c
test_modem_helpers_altair_lte_CPPFLAGS = \
	-I$(top_srcdir)/plugins/altair \
	$(PLUGIN_COMMON_COMPILER_FLAGS)
test_modem_helpers_altair_lte_LDADD = $(top_builddir)/libmm-glib/libmm-glib.la
test_modem_helpers_altair_lte_LDFLAGS = $(PLUGIN_COMMON_LINKER_FLAGS)


# VIA modem
libmm_plugin_via_la_SOURCES = \
	via/mm-plugin-via.c \
	via/mm-plugin-via.h \
	via/mm-broadband-modem-via.c \
	via/mm-broadband-modem-via.h
libmm_plugin_via_la_CPPFLAGS = $(PLUGIN_COMMON_COMPILER_FLAGS)
libmm_plugin_via_la_LDFLAGS = $(PLUGIN_COMMON_LINKER_FLAGS)

# Telit modem
libmm_plugin_telit_la_SOURCES = \
	telit/mm-plugin-telit.c \
	telit/mm-plugin-telit.h \
	telit/mm-broadband-modem-telit.c \
	telit/mm-broadband-modem-telit.h
libmm_plugin_telit_la_CPPFLAGS = $(PLUGIN_COMMON_COMPILER_FLAGS)
libmm_plugin_telit_la_LDFLAGS = $(PLUGIN_COMMON_LINKER_FLAGS)
udevrules_DATA += telit/77-mm-telit-port-types.rules

# MTK
libmm_plugin_mtk_la_SOURCES = \
	mtk/mm-plugin-mtk.c \
	mtk/mm-plugin-mtk.h \
	mtk/mm-broadband-modem-mtk.h \
	mtk/mm-broadband-modem-mtk.c
libmm_plugin_mtk_la_CPPFLAGS = $(PLUGIN_COMMON_COMPILER_FLAGS)
libmm_plugin_mtk_la_LDFLAGS = $(PLUGIN_COMMON_LINKER_FLAGS)
udevrules_DATA += mtk/77-mm-mtk-port-types.rules


# Additional files to include in the distribution
EXTRA_DIST += \
	$(udevrules_DATA) \
	tests/gsm-port.conf

# Unit tests
TEST_PROGS += $(noinst_PROGRAMS)
