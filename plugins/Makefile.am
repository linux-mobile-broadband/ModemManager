
# Common CPPFLAGS and LDFLAGS

PLUGIN_COMMON_COMPILER_FLAGS = \
	$(MM_CFLAGS) \
	$(GUDEV_CFLAGS)	\
	-I$(top_srcdir) \
	-I$(top_srcdir)/src \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/include \
	-I$(top_srcdir)/libmm-common \
	-I$(top_builddir)/libmm-common

PLUGIN_COMMON_LINKER_FLAGS = \
	$(GUDEV_LDFLAGS) \
	-module \
	-avoid-version

# UDev rules
udevrulesdir = $(UDEV_BASE_DIR)/rules.d
udevrules_DATA =

###########################
## Icera-specific support #
###########################
#
#noinst_LTLIBRARIES = libicera-utils.la
#
#libicera_utils_la_SOURCES = \
#	mm-modem-icera.c \
#	mm-modem-icera.h
#
#libicera_utils_la_CPPFLAGS = $(PLUGIN_COMMON_COMPILER_FLAGS)
#libicera_utils_la_LIBADD = $(GUDEV_LDFLAGS)

########################################

pkglib_LTLIBRARIES = \
	libmm-plugin-generic.la \
	libmm-plugin-cinterion.la \
	libmm-plugin-iridium.la \
	libmm-plugin-nokia.la \
	libmm-plugin-gobi.la \
	libmm-plugin-motorola.la \
	libmm-plugin-novatel.la \
	libmm-plugin-option.la \
	libmm-plugin-hso.la \
	libmm-plugin-anydata.la \
	libmm-plugin-linktop.la \
	libmm-plugin-simtech.la \
	libmm-plugin-wavecom.la

#pkglib_LTLIBRARIES = \
#	libmm-plugin-generic.la \
#	libmm-plugin-moto-c.la \
#	libmm-plugin-huawei.la \
#	libmm-plugin-sierra.la \
#	libmm-plugin-novatel.la \
#	libmm-plugin-nokia.la \
#	libmm-plugin-zte.la \
#	libmm-plugin-mbm.la \
#	libmm-plugin-longcheer.la \
#	libmm-plugin-anydata.la \
#	libmm-plugin-simtech.la \
#	libmm-plugin-x22x.la \
#	libmm-plugin-linktop.la \
#	libmm-plugin-samsung.la \
#	libmm-plugin-wavecom.la \
#	libmm-plugin-iridium.la

# Generic
libmm_plugin_generic_la_SOURCES = \
	generic/mm-plugin-generic.c \
	generic/mm-plugin-generic.h
libmm_plugin_generic_la_CPPFLAGS = $(PLUGIN_COMMON_COMPILER_FLAGS)
libmm_plugin_generic_la_LDFLAGS = $(PLUGIN_COMMON_LINKER_FLAGS)

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

#
## Huawei
#libmm_plugin_huawei_la_SOURCES = \
#	mm-plugin-huawei.c \
#	mm-plugin-huawei.h \
#	mm-modem-huawei-gsm.c \
#	mm-modem-huawei-gsm.h \
#	mm-modem-huawei-cdma.c \
#	mm-modem-huawei-cdma.h
#libmm_plugin_huawei_la_CPPFLAGS = $(PLUGIN_COMMON_COMPILER_FLAGS)
#libmm_plugin_huawei_la_LDFLAGS = $(PLUGIN_COMMON_LINKER_FLAGS)
#
## MBM
#libmm_plugin_mbm_la_SOURCES = \
#	mm-plugin-mbm.c \
#	mm-plugin-mbm.h \
#	mm-modem-mbm.c \
#	mm-modem-mbm.h
#libmm_plugin_mbm_la_CPPFLAGS = $(PLUGIN_COMMON_COMPILER_FLAGS)
#libmm_plugin_mbm_la_LDFLAGS = $(PLUGIN_COMMON_LINKER_FLAGS)

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

## Sierra
#libmm_plugin_sierra_la_SOURCES = \
#	mm-plugin-sierra.c \
#	mm-plugin-sierra.h \
#	mm-modem-sierra-gsm.c \
#	mm-modem-sierra-gsm.h \
#	mm-modem-sierra-cdma.c \
#	mm-modem-sierra-cdma.h
#libmm_plugin_sierra_la_CPPFLAGS = $(PLUGIN_COMMON_COMPILER_FLAGS)
#libmm_plugin_sierra_la_LDFLAGS = $(PLUGIN_COMMON_LINKER_FLAGS)
#libmm_plugin_sierra_la_LIBADD = \
#        $(builddir)/libicera-utils.la

# Wavecom (Sierra Airlink)
libmm_plugin_wavecom_la_SOURCES = \
	wavecom/mm-plugin-wavecom.c \
	wavecom/mm-plugin-wavecom.h \
	wavecom/mm-broadband-modem-wavecom.c \
	wavecom/mm-broadband-modem-wavecom.h
libmm_plugin_wavecom_la_CPPFLAGS = $(PLUGIN_COMMON_COMPILER_FLAGS)
libmm_plugin_wavecom_la_LDFLAGS = $(PLUGIN_COMMON_LINKER_FLAGS)

## Novatel
#libmm_plugin_novatel_la_SOURCES = \
#	mm-plugin-novatel.c \
#	mm-plugin-novatel.h \
#	mm-modem-novatel-gsm.c \
#	mm-modem-novatel-gsm.h \
#	mm-modem-novatel-cdma.c \
#	mm-modem-novatel-cdma.h
#libmm_plugin_novatel_la_CPPFLAGS = $(PLUGIN_COMMON_COMPILER_FLAGS)
#libmm_plugin_novatel_la_LDFLAGS = $(PLUGIN_COMMON_LINKER_FLAGS)

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
udevrules_DATA += nokia/77-mm-nokia-port-types.rules

## Zte
#libmm_plugin_zte_la_SOURCES = \
#	mm-plugin-zte.c \
#	mm-plugin-zte.h \
#	mm-modem-zte.c \
#	mm-modem-zte.h
#libmm_plugin_zte_la_CPPFLAGS = $(PLUGIN_COMMON_COMPILER_FLAGS)
#libmm_plugin_zte_la_LDFLAGS = $(PLUGIN_COMMON_LINKER_FLAGS)
#libmm_plugin_zte_la_LIBADD = $(builddir)/libicera-utils.la
#
## Longcheer (and rebranded dongles)
#libmm_plugin_longcheer_la_SOURCES = \
#	mm-plugin-longcheer.c \
#	mm-plugin-longcheer.h \
#	mm-modem-longcheer-gsm.c \
#	mm-modem-longcheer-gsm.h
#libmm_plugin_longcheer_la_CPPFLAGS = $(PLUGIN_COMMON_COMPILER_FLAGS)
#libmm_plugin_longcheer_la_LDFLAGS = $(PLUGIN_COMMON_LINKER_FLAGS)

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

#
## Alcatel/TCT/JRD x220D and possibly others
#libmm_plugin_x22x_la_SOURCES = \
#	mm-plugin-x22x.c \
#	mm-plugin-x22x.h \
#	mm-modem-x22x-gsm.c \
#	mm-modem-x22x-gsm.h
#libmm_plugin_x22x_la_CPPFLAGS = $(PLUGIN_COMMON_COMPILER_FLAGS)
#libmm_plugin_x22x_la_LDFLAGS = $(PLUGIN_COMMON_LINKER_FLAGS)
#
## Samsung modem
#libmm_plugin_samsung_la_SOURCES = \
#	mm-plugin-samsung.c \
#	mm-plugin-samsung.h \
#	mm-modem-samsung-gsm.c \
#	mm-modem-samsung-gsm.h
#libmm_plugin_samsung_la_CPPFLAGS = $(PLUGIN_COMMON_COMPILER_FLAGS)
#libmm_plugin_samsung_la_LDFLAGS = $(PLUGIN_COMMON_LINKER_FLAGS)
#libmm_plugin_samsung_la_LIBADD = $(builddir)/libicera-utils.la
#

# Cinterion (previously Siemens) modem
libmm_plugin_cinterion_la_SOURCES = \
	cinterion/mm-plugin-cinterion.c \
	cinterion/mm-plugin-cinterion.h \
	cinterion/mm-broadband-modem-cinterion.c \
	cinterion/mm-broadband-modem-cinterion.h
libmm_plugin_cinterion_la_CPPFLAGS = $(PLUGIN_COMMON_COMPILER_FLAGS)
libmm_plugin_cinterion_la_LDFLAGS = $(PLUGIN_COMMON_LINKER_FLAGS)

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

# Novatel modem
libmm_plugin_novatel_la_SOURCES = \
	novatel/mm-plugin-novatel.c \
	novatel/mm-plugin-novatel.h \
	novatel/mm-broadband-modem-novatel.c \
	novatel/mm-broadband-modem-novatel.h \
	novatel/mm-broadband-bearer-novatel.c \
	novatel/mm-broadband-bearer-novatel.h
libmm_plugin_novatel_la_CPPFLAGS = $(PLUGIN_COMMON_COMPILER_FLAGS)
libmm_plugin_novatel_la_LDFLAGS = $(PLUGIN_COMMON_LINKER_FLAGS)

#	77-mm-ericsson-mbm.rules \
#	77-mm-zte-port-types.rules \
#	77-mm-longcheer-port-types.rules \
#	77-mm-simtech-port-types.rules \
#	77-mm-x22x-port-types.rules \
#

# Additional files to include in the distribution
EXTRA_DIST = \
	$(udevrules_DATA)
